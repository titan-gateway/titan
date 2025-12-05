# JWT Authentication with JWKS

Titan supports JWT (JSON Web Token) authentication with dynamic key loading from JWKS (JSON Web Key Set) endpoints. This allows you to integrate with modern identity providers like Auth0, Keycloak, Google, and Microsoft without managing static keys.

## Features

- **Dynamic Key Loading**: Automatically fetch and refresh public keys from JWKS endpoints
- **Multiple Algorithms**: RS256 (RSA), ES256 (ECDSA P-256), HS256 (HMAC)
- **Circuit Breaker**: Fault-tolerant JWKS fetching with exponential backoff
- **Static Key Fallback**: Use static keys when JWKS is unavailable
- **Thread-Safe**: RCU (Read-Copy-Update) pattern for zero-lock key rotation
- **Token Caching**: LRU cache for validated tokens (10,000 tokens per worker)
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
jwt_iss → "https://auth0.example.com/"
jwt_jti → "token-id-123"
jwt_claims → "{...}"  (full JSON)
```

Use these in downstream middleware for authorization.

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
