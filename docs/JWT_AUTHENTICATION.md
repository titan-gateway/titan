# JWT Authentication with JWKS

Titan supports JWT (JSON Web Token) authentication with dynamic key loading from JWKS (JSON Web Key Set) endpoints. This allows you to integrate with modern identity providers like Auth0, Keycloak, Google, and Microsoft without managing static keys.

## Features

- **Dynamic Key Loading**: Automatically fetch and refresh public keys from JWKS endpoints
- **Multiple Algorithms**: RS256 (RSA), ES256 (ECDSA P-256), HS256 (HMAC)
- **Circuit Breaker**: Fault-tolerant JWKS fetching with exponential backoff
- **Static Key Fallback**: Use static keys when JWKS is unavailable
- **Thread-Safe**: RCU (Read-Copy-Update) pattern for zero-lock key rotation
- **Token Caching**: LRU cache for validated tokens (10,000 tokens per worker)
- **Token Revocation**: Immediate revocation with lock-free atomic queue and thread-local blacklists
- **Standards Compliant**: RFC 7519 (JWT), RFC 7517 (JWK), RFC 4648 (Base64url)

## Configuration

### Basic JWT with JWKS

```json
{
  "jwt": {
    "enabled": true,
    "header": "Authorization",
    "scheme": "Bearer",

    "jwks": {
      "url": "https://your-tenant.auth0.com/.well-known/jwks.json",
      "refresh_interval_seconds": 3600,
      "timeout_seconds": 10,
      "retry_max": 3,
      "circuit_breaker_seconds": 300
    },

    "require_exp": true,
    "require_sub": true,
    "clock_skew_seconds": 60,
    "allowed_issuers": ["https://your-tenant.auth0.com/"],
    "allowed_audiences": ["https://api.example.com"],

    "cache_enabled": true,
    "cache_capacity": 10000
  }
}
```

### JWKS Configuration Options

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `url` | string | required | JWKS endpoint URL (Auth0, Keycloak, etc.) |
| `refresh_interval_seconds` | uint32 | 3600 | How often to refresh keys (1 hour) |
| `timeout_seconds` | uint32 | 10 | HTTP request timeout |
| `retry_max` | uint32 | 3 | Max failures before circuit breaker opens |
| `circuit_breaker_seconds` | uint32 | 300 | Cooldown period when circuit is open (5 min) |

### Static Key Fallback

You can configure static keys as fallback when JWKS is unavailable:

```json
{
  "jwt": {
    "jwks": { ... },

    "keys": [
      {
        "algorithm": "RS256",
        "key_id": "static-rsa-key-1",
        "public_key_path": "/etc/titan/keys/rsa-public.pem"
      },
      {
        "algorithm": "ES256",
        "key_id": "static-ec-key-1",
        "public_key_path": "/etc/titan/keys/ec-public.pem"
      },
      {
        "algorithm": "HS256",
        "key_id": "static-hmac-key-1",
        "secret": "base64-encoded-secret"
      }
    ]
  }
}
```

## Identity Provider Examples

### Auth0

```json
{
  "jwt": {
    "jwks": {
      "url": "https://YOUR_TENANT.auth0.com/.well-known/jwks.json"
    },
    "allowed_issuers": ["https://YOUR_TENANT.auth0.com/"],
    "allowed_audiences": ["YOUR_API_IDENTIFIER"]
  }
}
```

### Keycloak

```json
{
  "jwt": {
    "jwks": {
      "url": "https://keycloak.example.com/realms/YOUR_REALM/protocol/openid-connect/certs"
    },
    "allowed_issuers": ["https://keycloak.example.com/realms/YOUR_REALM"],
    "allowed_audiences": ["account"]
  }
}
```

### Google

```json
{
  "jwt": {
    "jwks": {
      "url": "https://www.googleapis.com/oauth2/v3/certs"
    },
    "allowed_issuers": ["https://accounts.google.com"],
    "allowed_audiences": ["YOUR_CLIENT_ID"]
  }
}
```

### Microsoft Azure AD

```json
{
  "jwt": {
    "jwks": {
      "url": "https://login.microsoftonline.com/YOUR_TENANT/discovery/v2.0/keys"
    },
    "allowed_issuers": ["https://login.microsoftonline.com/YOUR_TENANT/v2.0"],
    "allowed_audiences": ["YOUR_CLIENT_ID"]
  }
}
```

## How It Works

### 1. Dynamic Key Loading

The JWKS fetcher runs in a background thread for each worker:

```
[Start] → [Fetch JWKS] → [Parse JWKs] → [Convert to EVP_PKEY] → [RCU Update] → [Sleep 1h] → [Repeat]
```

- Keys are fetched immediately on startup
- Refreshed every hour (configurable)
- RCU pattern ensures zero-lock reads during updates
- Failed fetches increment circuit breaker counter

### 2. Circuit Breaker States

```
[Healthy] → (3 failures) → [Degraded] → (retry_max failures) → [CircuitOpen]
                              ↑                                        ↓
                              └────────── (successful fetch) ─────────┘
```

- **Healthy**: JWKS fetching successfully
- **Degraded**: Using cached keys, JWKS fetch failing
- **CircuitOpen**: Circuit breaker open, using static fallback keys

### 3. Key Priority

When validating JWTs, keys are checked in this order:

1. **JWKS keys** (if available and circuit breaker closed)
2. **Static keys** (fallback when JWKS unavailable)

Keys are matched by:
- Algorithm (RS256, ES256, HS256)
- Key ID (`kid` header in JWT)

### 4. Token Validation Flow

```
Client Request
    ↓
Extract "Authorization: Bearer <token>"
    ↓
Check LRU cache (hit → return cached claims)
    ↓
Parse JWT (header.payload.signature)
    ↓
Get key from JWKS/static (match alg + kid)
    ↓
Verify signature (RSA/ECDSA/HMAC)
    ↓
Validate claims (exp, nbf, iss, aud, sub)
    ↓
Cache result + continue request
```

## Claims Validation

### Standard Claims

| Claim | Field | Validation |
|-------|-------|------------|
| `exp` | Expiration | Must be in future (with clock skew) |
| `nbf` | Not Before | Must be in past (with clock skew) |
| `iat` | Issued At | Informational only |
| `iss` | Issuer | Must match `allowed_issuers` |
| `aud` | Audience | Must match `allowed_audiences` |
| `sub` | Subject | Required if `require_sub: true` |
| `jti` | JWT ID | Stored in context for revocation |

### Custom Claims

Custom claims (e.g., `scope`, `role`) are stored in request metadata:

```
jwt_sub → "user123"
jwt_scope → "read:users write:posts"
jwt_roles → "admin moderator"
jwt_iss → "https://auth0.example.com/"
jwt_jti → "token-id-123"
jwt_claims → "{...}"  (full JSON)
```

These claims are extracted from the JWT and made available to authorization middleware (see **JWT Authorization** section below).

## JWT Authorization (Claims-Based Access Control)

Titan provides fine-grained authorization based on JWT claims. After a JWT is authenticated, the authorization middleware checks if the user has the required **scopes** (OAuth 2.0 permissions) or **roles** to access a specific route.

### Features

- **OAuth 2.0 Scopes**: Space-separated permissions (e.g., `"read:users write:posts"`)
- **Custom Roles**: Simple role strings (e.g., `"admin moderator"`)
- **Flexible Matching**: AND/OR logic for scopes and roles
- **Per-Route Configuration**: Different authorization requirements for each route
- **Detailed Error Responses**: 403 Forbidden with JSON error body

### Configuration

Authorization is configured per route in the `routes` section:

```json
{
  "routes": [
    {
      "path": "/api/users",
      "method": "GET",
      "upstream": "users-service",
      "auth_required": true,
      "required_scopes": ["read:users"]
    },
    {
      "path": "/api/users",
      "method": "POST",
      "upstream": "users-service",
      "auth_required": true,
      "required_scopes": ["write:users"],
      "required_roles": ["admin"]
    }
  ]
}
```

### Authorization Middleware Configuration

Global authorization settings (optional):

```json
{
  "jwt_authz": {
    "enabled": true,
    "scope_claim": "scope",
    "roles_claim": "roles",
    "require_all_scopes": false,
    "require_all_roles": false
  }
}
```

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `enabled` | bool | true | Enable/disable authorization middleware |
| `scope_claim` | string | "scope" | JWT claim containing scopes |
| `roles_claim` | string | "roles" | JWT claim containing roles |
| `require_all_scopes` | bool | false | `false` = OR logic (any scope), `true` = AND logic (all scopes) |
| `require_all_roles` | bool | false | `false` = OR logic (any role), `true` = AND logic (all roles) |

### Scope Matching

#### OR Logic (Default)

User needs **at least one** of the required scopes:

```json
{
  "path": "/api/posts",
  "method": "GET",
  "required_scopes": ["read:posts", "read:all"],
  "jwt_authz": { "require_all_scopes": false }
}
```

✅ Allowed if JWT has: `"scope": "read:posts"`
✅ Allowed if JWT has: `"scope": "read:all"`
✅ Allowed if JWT has: `"scope": "read:posts write:posts"`
❌ Denied if JWT has: `"scope": "write:posts"`

#### AND Logic

User needs **all** of the required scopes:

```json
{
  "path": "/api/admin/users",
  "method": "DELETE",
  "required_scopes": ["delete:users", "admin:access"],
  "jwt_authz": { "require_all_scopes": true }
}
```

✅ Allowed if JWT has: `"scope": "delete:users admin:access"`
❌ Denied if JWT has: `"scope": "delete:users"`
❌ Denied if JWT has: `"scope": "admin:access"`

### Role Matching

Roles work the same way as scopes:

#### OR Logic (Default)

User needs **at least one** of the required roles:

```json
{
  "path": "/api/admin/dashboard",
  "method": "GET",
  "required_roles": ["admin", "moderator"]
}
```

✅ Allowed if JWT has: `"roles": "admin"`
✅ Allowed if JWT has: `"roles": "moderator"`
❌ Denied if JWT has: `"roles": "user"`

#### AND Logic

User needs **all** of the required roles:

```json
{
  "path": "/api/super-admin",
  "method": "POST",
  "required_roles": ["admin", "super-admin"],
  "jwt_authz": { "require_all_roles": true }
}
```

✅ Allowed if JWT has: `"roles": "admin super-admin"`
❌ Denied if JWT has: `"roles": "admin"`

### Combined Scope and Role Requirements

Routes can require **both** scopes and roles:

```json
{
  "path": "/api/admin/users",
  "method": "POST",
  "auth_required": true,
  "required_scopes": ["write:users"],
  "required_roles": ["admin"]
}
```

Access is granted only if user has:
- **All required scopes** (respecting AND/OR logic)
- **AND all required roles** (respecting AND/OR logic)

### Identity Provider Examples

#### Auth0 with Scopes

1. Configure API in Auth0 with scopes:
   - `read:users`
   - `write:users`
   - `delete:users`

2. Token will contain:
```json
{
  "iss": "https://your-tenant.auth0.com/",
  "sub": "auth0|123456",
  "aud": "https://api.example.com",
  "scope": "read:users write:posts"
}
```

3. Configure Titan routes:
```json
{
  "routes": [
    {
      "path": "/api/users",
      "method": "GET",
      "required_scopes": ["read:users"]
    }
  ]
}
```

#### Keycloak with Roles

1. Create roles in Keycloak realm:
   - `admin`
   - `moderator`
   - `user`

2. Map roles to JWT claim (Keycloak client mapper):
   - Token Claim Name: `roles`
   - Claim JSON Type: String (space-separated)

3. Token will contain:
```json
{
  "iss": "https://keycloak.example.com/realms/myrealm",
  "sub": "user-uuid",
  "roles": "admin moderator"
}
```

4. Configure Titan routes:
```json
{
  "routes": [
    {
      "path": "/api/admin",
      "method": "POST",
      "required_roles": ["admin"]
    }
  ]
}
```

### Error Responses

When authorization fails, Titan returns HTTP 403 Forbidden:

```http
HTTP/1.1 403 Forbidden
Content-Type: application/json

{
  "error": "forbidden",
  "message": "Insufficient permissions"
}
```

The error message is intentionally generic to avoid leaking authorization logic. Detailed failure reasons are logged server-side:

```
[WARN] Authorization failed: missing scopes, user_scopes=read:posts, required_scopes=write:posts, client_ip=192.168.1.1, correlation_id=req-123
```

### Best Practices

1. **Use Scopes for Permissions**: Model permissions as OAuth 2.0 scopes (`read:resource`, `write:resource`)
2. **Use Roles for Groups**: Model user groups as simple roles (`admin`, `moderator`, `user`)
3. **Combine Both**: Use scopes for fine-grained permissions and roles for broad access levels
4. **Start with OR Logic**: Use `require_all_scopes: false` for most routes (easier to manage)
5. **Use AND Logic Sparingly**: Reserve `require_all_scopes: true` for highly sensitive operations
6. **Namespace Scopes**: Use colons to namespace scopes (`resource:action` format)
7. **Keep Roles Simple**: Avoid creating too many granular roles (use scopes instead)

### Testing

Run authorization tests:

```bash
./build/dev/tests/unit/titan_tests '[jwt][authz]'
```

Test with curl:

```bash
# Get token with specific scopes
TOKEN=$(curl -X POST https://your-tenant.auth0.com/oauth/token \
  -H 'Content-Type: application/json' \
  -d '{
    "client_id": "YOUR_CLIENT_ID",
    "client_secret": "YOUR_CLIENT_SECRET",
    "audience": "YOUR_API",
    "grant_type": "client_credentials",
    "scope": "read:users write:posts"
  }' | jq -r '.access_token')

# Test authorized request
curl -H "Authorization: Bearer $TOKEN" \
  http://localhost:8080/api/users

# Test unauthorized request (should return 403)
curl -H "Authorization: Bearer $TOKEN" \
  http://localhost:8080/api/admin
```

## JWT Token Revocation

Titan supports immediate JWT token revocation for security-critical scenarios (e.g., compromised tokens, logout, permission changes). Revoked tokens are rejected even if they haven't expired yet.

### Features

- **Immediate Revocation**: Tokens are revoked immediately without waiting for expiration
- **Lock-Free Architecture**: Atomic queue for cross-thread communication, zero-lock reads
- **Thread-Local Blacklist**: Each worker maintains its own revocation list (shared-nothing)
- **Automatic Cleanup**: Expired revocations are automatically removed to free memory
- **Admin API**: Simple HTTP endpoint for revoking tokens
- **JTI-Based**: Uses JWT ID (`jti` claim) for unique token identification

### Configuration

Enable token revocation in your JWT configuration:

```json
{
  "jwt": {
    "enabled": true,
    "revocation_enabled": true,
    "jwks": { ... }
  }
}
```

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `revocation_enabled` | bool | true | Enable/disable token revocation checking |

### Architecture

Revocation uses a **lock-free atomic queue** for broadcasting and **thread-local blacklists** for validation:

```
Admin API (POST /_admin/jwt/revoke)
    ↓
Push to Global RevocationQueue (lock-free atomic stack)
    ↓
Workers sync on each request (fast atomic check if queue empty)
    ↓
Thread-Local RevocationList (O(1) lookup, no locks)
    ↓
Reject revoked tokens with 401 Unauthorized
```

**Key Design Decisions:**

1. **Lock-Free Queue**: Uses compare-and-swap (CAS) for wait-free push operations
2. **Thread-Local Lists**: Each worker owns its blacklist (no lock contention on hot path)
3. **Fast Path Optimization**: Workers check `has_pending()` via atomic load (no syscalls)
4. **RCU Pattern**: Workers drain queue and update their local blacklist atomically
5. **Memory Management**: Expired entries are cleaned up automatically

### Admin API Endpoint

Revoke a token using the admin API:

```bash
curl -X POST http://localhost:9090/_admin/jwt/revoke \
  -H 'Content-Type: application/json' \
  -d '{
    "jti": "token-id-to-revoke",
    "exp": 1735730400
  }'
```

**Request Body:**

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `jti` | string | Yes | JWT ID claim from the token to revoke |
| `exp` | uint64 | Yes | Token expiration timestamp (Unix epoch seconds) |

**Response (Success):**

```json
{
  "status": "ok",
  "message": "Token revoked successfully"
}
```

**Response (Error):**

```json
{
  "error": "bad_request",
  "message": "Missing or invalid 'jti' field"
}
```

**Important**: The `exp` field is required (not optional). This prevents memory waste and security issues:
- If token expires in 5 minutes but you set `exp` to 1 hour → waste memory
- If token expires in 24 hours but you set `exp` to 1 hour → token becomes valid again (security issue!)

### Getting JTI from a Token

Extract the `jti` claim from a JWT token:

```bash
# Decode JWT payload (base64url decode)
TOKEN="eyJhbGc..."
PAYLOAD=$(echo "$TOKEN" | cut -d '.' -f 2 | base64 -d 2>/dev/null)
echo "$PAYLOAD" | jq -r '.jti, .exp'
```

Example JWT payload:

```json
{
  "iss": "https://auth0.example.com/",
  "sub": "user123",
  "aud": "https://api.example.com",
  "exp": 1735730400,
  "iat": 1735726800,
  "jti": "unique-token-id-12345"
}
```

### Worker Synchronization

Each worker thread synchronizes revocations on every request:

1. **Fast Path** (queue empty): Single atomic load, ~1 ns overhead
2. **Slow Path** (queue has entries): Drain queue, update local blacklist, ~1 μs

Workers sync independently:
- First worker to sync drains the entire queue
- Other workers find queue empty (fast path)
- Eventually consistent (all workers see revocations within ~1 request cycle)

### Validation Flow

When a request arrives with a JWT token:

```
Extract JWT → Parse & Verify Signature → Validate Claims (exp, iss, aud)
    ↓
Check if jti is in revocation blacklist
    ↓
[YES] → 401 Unauthorized "Token has been revoked"
    ↓
[NO] → Continue to authorization middleware
```

The revocation check happens in `JwtAuthMiddleware::process_request()` at `src/gateway/jwt_middleware.cpp:183-195`.

### Performance

- **Revocation Check**: O(1) hash map lookup, ~10 ns overhead per request
- **Sync from Queue**: ~1 μs when queue has entries, ~1 ns when empty
- **Memory Usage**: ~64 bytes per revoked token (jti string + exp timestamp)
- **Cleanup**: Automatic removal of expired entries (no manual intervention needed)

### Testing

Run revocation unit tests:

```bash
./build/dev/tests/unit/titan_tests '[jwt][revocation]'
```

Test with curl:

```bash
# 1. Get a valid token
TOKEN=$(curl -X POST https://your-tenant.auth0.com/oauth/token \
  -H 'Content-Type: application/json' \
  -d '{
    "client_id": "YOUR_CLIENT_ID",
    "client_secret": "YOUR_CLIENT_SECRET",
    "audience": "YOUR_API",
    "grant_type": "client_credentials"
  }' | jq -r '.access_token')

# 2. Test token is valid
curl -H "Authorization: Bearer $TOKEN" \
  http://localhost:8080/api/users
# → 200 OK

# 3. Revoke the token
# Extract jti and exp from token
PAYLOAD=$(echo "$TOKEN" | cut -d '.' -f 2 | base64 -d 2>/dev/null)
JTI=$(echo "$PAYLOAD" | jq -r '.jti')
EXP=$(echo "$PAYLOAD" | jq -r '.exp')

curl -X POST http://localhost:9090/_admin/jwt/revoke \
  -H 'Content-Type: application/json' \
  -d "{\"jti\":\"$JTI\",\"exp\":$EXP}"
# → {"status":"ok","message":"Token revoked successfully"}

# 4. Test token is now revoked
curl -H "Authorization: Bearer $TOKEN" \
  http://localhost:8080/api/users
# → 401 Unauthorized {"error":"unauthorized","message":"Token has been revoked"}
```

### Best Practices

1. **Always Include JTI**: Ensure your identity provider includes `jti` claim in JWTs
2. **Use Correct Expiration**: Always provide the actual token `exp` value when revoking
3. **Revoke Before Expiry**: Revoke tokens as soon as possible when compromised
4. **Short-Lived Tokens**: Use short expiration times (5-15 minutes) to minimize revocation overhead
5. **Refresh Tokens**: Use refresh tokens for long-lived sessions, revoke refresh tokens instead
6. **Monitor Revocations**: Track revocation metrics to detect abuse patterns
7. **Cleanup Strategy**: Let automatic cleanup handle expired entries (no manual intervention)
8. **Secure Admin API**: Protect the `/_admin/jwt/revoke` endpoint with authentication

### Use Cases

**1. User Logout**

When a user logs out, revoke their current access token:

```bash
# Extract jti from token
curl -X POST http://localhost:9090/_admin/jwt/revoke \
  -H 'Content-Type: application/json' \
  -d '{"jti":"user-session-token-123","exp":1735730400}'
```

**2. Compromised Token**

If a token is leaked or compromised:

```bash
# Immediately revoke the compromised token
curl -X POST http://localhost:9090/_admin/jwt/revoke \
  -H 'Content-Type: application/json' \
  -d '{"jti":"compromised-token-456","exp":1735730400}'
```

**3. Permission Changes**

When a user's permissions change (e.g., admin → user):

```bash
# Revoke old token with admin permissions
curl -X POST http://localhost:9090/_admin/jwt/revoke \
  -H 'Content-Type: application/json' \
  -d '{"jti":"old-admin-token-789","exp":1735730400}'

# User gets new token with updated permissions on next refresh
```

### Monitoring

Revocation metrics (Prometheus format):

```
titan_jwt_revocations_total 150
titan_jwt_revocation_checks_total{result="allowed"} 100000
titan_jwt_revocation_checks_total{result="revoked"} 50
titan_jwt_revocation_queue_size 0
titan_jwt_revocation_blacklist_size{worker="0"} 25
titan_jwt_revocation_blacklist_size{worker="1"} 25
titan_jwt_revocation_blacklist_size{worker="2"} 25
titan_jwt_revocation_blacklist_size{worker="3"} 25
```

### Limitations

1. **Requires JTI Claim**: Tokens must include `jti` claim to be revokable
2. **Eventually Consistent**: Workers sync independently (not instant across all workers)
3. **Memory Overhead**: Each revoked token consumes ~64 bytes until expiration
4. **No Persistence**: Revocations are in-memory only (lost on restart)

For persistent revocation (surviving restarts), consider:
- Redis-backed revocation list
- Database-backed revocation table
- Shorter token expiration times (less need for revocation)

## Performance

### Benchmarks

- **Token validation**: ~10 μs (cached)
- **Token validation**: ~50 μs (uncached, RSA-SHA256)
- **Token validation**: ~30 μs (uncached, ECDSA-P256)
- **JWKS fetch**: ~100-500 ms (HTTP + parsing)
- **Key rotation**: 0 ns (lock-free RCU)

### Caching

- LRU cache per worker thread (10,000 tokens default)
- Cache hit rate: >95% in production
- Memory usage: ~100 bytes per cached token

### Thread Safety

- **JWKS keys**: Atomic pointer swap (RCU)
- **Static keys**: Immutable after load
- **Token cache**: Thread-local (no locking)

## Testing

### Unit Tests

```bash
# Run JWT tests
./build/dev/tests/unit/titan_tests '[jwt]'

# Run JWKS tests
./build/dev/tests/unit/titan_tests '[jwks]'

# Run integration tests
./build/dev/tests/unit/titan_tests '[jwt][jwks][integration]'
```

### Manual Testing with Auth0

1. Create Auth0 account and API
2. Get JWKS URL from Auth0 dashboard
3. Configure Titan with JWKS URL
4. Generate test token from Auth0
5. Test with curl:

```bash
# Get token from Auth0
TOKEN=$(curl -X POST https://YOUR_TENANT.auth0.com/oauth/token \
  -H 'Content-Type: application/json' \
  -d '{
    "client_id": "YOUR_CLIENT_ID",
    "client_secret": "YOUR_CLIENT_SECRET",
    "audience": "YOUR_API_IDENTIFIER",
    "grant_type": "client_credentials"
  }' | jq -r '.access_token')

# Test with Titan
curl -H "Authorization: Bearer $TOKEN" http://localhost:8080/api/protected
```

## Monitoring

### Metrics (Prometheus format)

```
titan_jwt_validations_total{result="success"} 10000
titan_jwt_validations_total{result="expired"} 50
titan_jwt_validations_total{result="invalid_signature"} 10
titan_jwt_cache_hits_total 9500
titan_jwt_cache_misses_total 500
titan_jwks_fetch_success_total 24
titan_jwks_fetch_failures_total 1
titan_jwks_circuit_breaker_state{state="healthy"} 1
```

### Health Checks

Check JWKS fetcher state via admin API:

```bash
curl http://localhost:9090/admin/jwks/status
```

Response:
```json
{
  "state": "healthy",
  "last_success_timestamp": 1735730000,
  "consecutive_failures": 0,
  "key_count": 3
}
```

## Troubleshooting

### Common Issues

**Issue**: `Unknown key ID` error

**Solution**:
- Check JWT `kid` header matches JWKS endpoint
- Verify JWKS URL is correct
- Check circuit breaker isn't open: `curl http://localhost:9090/admin/jwks/status`

**Issue**: `Invalid signature` error

**Solution**:
- Verify token is from correct issuer
- Check token hasn't been tampered with
- Ensure clock synchronization (NTP)

**Issue**: `Token expired` error

**Solution**:
- Check server time is correct
- Increase `clock_skew_seconds` if needed
- Ensure client refreshes tokens before expiry

**Issue**: JWKS circuit breaker stuck open

**Solution**:
- Check JWKS URL is reachable: `curl -v https://your-tenant.auth0.com/.well-known/jwks.json`
- Verify firewall/DNS settings
- Check `retry_max` and `circuit_breaker_seconds` configuration
- Use static keys as fallback

## Security Considerations

1. **HTTPS Required**: Always use HTTPS for JWKS URLs to prevent MITM attacks
2. **Key Rotation**: JWKS automatically handles key rotation from identity provider
3. **Token Expiry**: Always set short expiration times (`exp` claim)
4. **Audience Validation**: Always validate `aud` claim to prevent token reuse
5. **Issuer Validation**: Always validate `iss` claim to prevent token substitution
6. **No `none` Algorithm**: The `none` algorithm is rejected by default
7. **Static Key Security**: Store static keys in secure locations (`/etc/titan/keys/`)
8. **Secret Rotation**: Rotate HMAC secrets regularly

## Example Configuration

See `config/jwt-jwks-example.json` for a complete working example.
