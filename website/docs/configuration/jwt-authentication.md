---
sidebar_position: 3
title: JWT Authentication
---

# JWT Authentication

Titan supports industry-standard JWT (RFC 7519) authentication with RS256, ES256, and HS256 signature algorithms.

## Quick Start

```json
{
  "jwt": {
    "enabled": true,
    "keys": [
      {
        "algorithm": "RS256",
        "key_id": "primary",
        "public_key_path": "/etc/titan/keys/jwt_public.pem"
      }
    ],
    "allowed_issuers": ["https://auth.example.com"],
    "allowed_audiences": ["api.example.com"]
  },
  "routes": [
    {
      "path": "/api/*",
      "upstream": "backend"
    }
  ]
}
```

## Generating Keys

### RSA Keys (RS256)

```bash
# Generate private key (2048-bit minimum, 4096-bit recommended)
openssl genrsa -out jwt_private.pem 4096

# Extract public key
openssl rsa -in jwt_private.pem -pubout -out jwt_public.pem
```

### ECDSA Keys (ES256)

```bash
# Generate private key (P-256 curve)
openssl ecparam -genkey -name prime256v1 -out jwt_private_ec.pem

# Extract public key
openssl ec -in jwt_private_ec.pem -pubout -out jwt_public_ec.pem
```

### HMAC Secret (HS256)

```bash
# Generate random secret (base64-encoded)
openssl rand -base64 32
```

## Configuration Reference

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `enabled` | boolean | `false` | Enable JWT authentication |
| `header` | string | `"Authorization"` | Header containing JWT token |
| `scheme` | string | `"Bearer"` | Authorization scheme (e.g., "Bearer") |
| `keys[]` | array | `[]` | Verification keys (supports multiple for rotation) |
| `require_exp` | boolean | `true` | Require expiration claim |
| `require_sub` | boolean | `false` | Require subject claim |
| `allowed_issuers` | array | `[]` | Whitelist of valid issuers (empty = allow all) |
| `allowed_audiences` | array | `[]` | Whitelist of valid audiences (empty = allow all) |
| `clock_skew_seconds` | number | `60` | Tolerance for time-based claims (prevents clock drift issues) |
| `cache_capacity` | number | `10000` | Tokens cached per worker thread |
| `cache_enabled` | boolean | `true` | Enable thread-local token cache |

### Key Configuration

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `algorithm` | string | Yes | `"RS256"`, `"ES256"`, or `"HS256"` |
| `key_id` | string | No | Key identifier (matches JWT `kid` header) |
| `public_key_path` | string | RS256/ES256 | Path to PEM-encoded public key |
| `secret` | string | HS256 | Base64-encoded HMAC secret |

## Supported Algorithms

### RS256 (Recommended for Production)

- **Type**: Asymmetric (RSA + SHA-256)
- **Key Size**: 2048-bit minimum (4096-bit recommended)
- **Use Case**: Tokens signed by external auth server (Auth0, Keycloak, etc.)

Public key can be distributed, private key stays on auth server.

### ES256

- **Type**: Asymmetric (ECDSA P-256 + SHA-256)
- **Key Size**: 256-bit
- **Use Case**: Modern systems, mobile apps

Faster than RS256 with smaller keys.

### HS256

- **Type**: Symmetric (HMAC + SHA-256)
- **Use Case**: Internal microservices where Titan shares the secret

**Warning**: Titan can forge tokens with HS256. Use only for trusted internal services.

## Key Rotation

Titan supports multiple keys simultaneously for zero-downtime key rotation:

```json
{
  "jwt": {
    "enabled": true,
    "keys": [
      {
        "algorithm": "RS256",
        "key_id": "primary",
        "public_key_path": "/etc/titan/keys/jwt_public_new.pem"
      },
      {
        "algorithm": "RS256",
        "key_id": "secondary",
        "public_key_path": "/etc/titan/keys/jwt_public_old.pem"
      }
    ]
  }
}
```

**Rotation Process:**
1. Add new key to configuration (don't remove old key yet)
2. Hot-reload Titan: `kill -HUP $(pidof titan)`
3. Update auth server to sign new tokens with new key
4. Wait for all old tokens to expire
5. Remove old key from Titan configuration

## Claims Validation

Titan validates standard JWT claims automatically:

| Claim | Description | Validation |
|-------|-------------|------------|
| `exp` | Expiration time | Token rejected if `exp < now` (with clock skew tolerance) |
| `nbf` | Not before | Token rejected if `nbf > now` (with clock skew tolerance) |
| `iss` | Issuer | Token rejected if issuer not in `allowed_issuers` whitelist |
| `aud` | Audience | Token rejected if audience not in `allowed_audiences` whitelist |
| `sub` | Subject (user ID) | Optional, stored in context if present |

### Clock Skew Tolerance

The `clock_skew_seconds` setting (default 60s) prevents validation failures due to clock drift between auth server and Titan:

```
Token valid if: exp + clock_skew > now
Token valid if: nbf - clock_skew < now
```

## Claims in Middleware Context

Validated JWT claims are stored in `RequestContext.metadata` and available to downstream middleware:

- `jwt_sub` - Subject (user ID)
- `jwt_scope` - Permissions/scopes
- `jwt_iss` - Issuer
- `jwt_jti` - Token ID (for revocation)

### Example: Rate Limiting by User ID

Use JWT subject for rate limiting instead of IP address:

```json
{
  "jwt": { "enabled": true },
  "rate_limit": {
    "enabled": true,
    "key": "jwt_sub",
    "requests_per_second": 100
  }
}
```

## Security Best Practices

### 1. Use Asymmetric Algorithms (RS256/ES256)

Prefer RS256 or ES256 over HS256 in production—Titan cannot forge tokens with asymmetric algorithms.

### 2. Validate Issuer and Audience

Always configure `allowed_issuers` and `allowed_audiences` to prevent token reuse across services:

```json
{
  "jwt": {
    "allowed_issuers": ["https://auth.your-company.com"],
    "allowed_audiences": ["api.your-company.com"]
  }
}
```

### 3. Use Short-Lived Tokens

Set token expiration (`exp`) to 15 minutes or less, use refresh tokens for long sessions:

```json
{
  "exp": 1735730900,  // 15 minutes from issue time
  "iat": 1735730000
}
```

### 4. Don't Log Tokens

Never log full JWT tokens—they contain sensitive information and can be replayed if leaked.

### 5. Rotate Keys Regularly

Rotate signing keys quarterly or after security incidents using the multi-key configuration.

## Error Responses

Invalid tokens return **401 Unauthorized** with RFC 6750-compliant headers:

```http
HTTP/1.1 401 Unauthorized
WWW-Authenticate: Bearer realm="titan"
Content-Type: application/json

{"error":"unauthorized","message":"Authentication required"}
```

**Common Errors:**
- Missing Authorization header
- Invalid token format (not "Bearer `<token>`")
- Malformed JWT (invalid base64url encoding)
- Invalid signature
- Expired token (`exp` claim)
- Token not yet valid (`nbf` claim)
- Invalid issuer/audience
- Unknown key ID (`kid`)

## Example: Full Configuration

```json
{
  "server": {
    "port": 8080,
    "workers": 4,
    "tls": {
      "enabled": true,
      "cert_file": "/etc/titan/certs/server.crt",
      "key_file": "/etc/titan/certs/server.key"
    }
  },
  "jwt": {
    "enabled": true,
    "keys": [
      {
        "algorithm": "RS256",
        "key_id": "prod-2025",
        "public_key_path": "/etc/titan/keys/jwt_public.pem"
      }
    ],
    "allowed_issuers": ["https://auth.example.com"],
    "allowed_audiences": ["api.example.com"],
    "require_exp": true,
    "clock_skew_seconds": 60
  },
  "upstreams": [
    {
      "name": "api",
      "backends": [
        { "host": "api-backend", "port": 8080 }
      ]
    }
  ],
  "routes": [
    {
      "path": "/api/*",
      "upstream": "api"
    }
  ]
}
```

## Testing with curl

```bash
# 1. Generate test token (use your auth server or jwt.io)
TOKEN="eyJhbGciOiJSUzI1NiIsInR5cCI6IkpXVCJ9..."

# 2. Make authenticated request
curl -H "Authorization: Bearer $TOKEN" \
     https://localhost:8080/api/users

# 3. Test invalid token (should return 401)
curl -H "Authorization: Bearer invalid" \
     https://localhost:8080/api/users
```

## JWT Authorization (Claims-Based Access Control)

After authenticating a JWT, Titan provides fine-grained authorization based on JWT claims. Use **OAuth 2.0 scopes** for permissions and **roles** for user groups.

### Per-Route Authorization

Configure authorization requirements directly on routes:

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

### Authorization Configuration

Global settings for authorization behavior:

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
| `enabled` | boolean | `true` | Enable authorization middleware |
| `scope_claim` | string | `"scope"` | JWT claim containing scopes |
| `roles_claim` | string | `"roles"` | JWT claim containing roles |
| `require_all_scopes` | boolean | `false` | `false` = OR (any), `true` = AND (all) |
| `require_all_roles` | boolean | `false` | `false` = OR (any), `true` = AND (all) |

### Scope Matching

#### OR Logic (Default) - Any Scope

User needs **at least one** of the required scopes:

```json
{
  "path": "/api/posts",
  "method": "GET",
  "required_scopes": ["read:posts", "read:all"]
}
```

✅ Allowed: `"scope": "read:posts"`
✅ Allowed: `"scope": "read:all"`
❌ Denied: `"scope": "write:posts"`

#### AND Logic - All Scopes

User needs **all** required scopes:

```json
{
  "path": "/api/admin/users",
  "method": "DELETE",
  "required_scopes": ["delete:users", "admin:access"],
  "jwt_authz": { "require_all_scopes": true }
}
```

✅ Allowed: `"scope": "delete:users admin:access"`
❌ Denied: `"scope": "delete:users"` (missing admin:access)

### Role Matching

Roles work identically to scopes:

```json
{
  "path": "/api/admin/dashboard",
  "method": "GET",
  "required_roles": ["admin", "moderator"]
}
```

✅ Allowed: `"roles": "admin"`
✅ Allowed: `"roles": "moderator"`
❌ Denied: `"roles": "user"`

### Authorization Error Response

Failed authorization returns **403 Forbidden**:

```http
HTTP/1.1 403 Forbidden
Content-Type: application/json

{"error":"forbidden","message":"Insufficient permissions"}
```

Server logs contain detailed failure reasons (scopes/roles that were missing).

### Best Practices

1. **Use Scopes for Permissions**: Model permissions as OAuth 2.0 scopes (`read:resource`, `write:resource`)
2. **Use Roles for Groups**: Model user groups as simple roles (`admin`, `moderator`, `user`)
3. **Start with OR Logic**: Use `require_all_scopes: false` for most routes (easier to manage)
4. **Namespace Scopes**: Use colons to namespace scopes (`resource:action` format)
5. **Keep Roles Simple**: Avoid creating too many granular roles (use scopes instead)

## Troubleshooting

### "Invalid signature" errors

- Ensure public key matches the private key used to sign tokens
- Verify PEM file format (should start with `-----BEGIN PUBLIC KEY-----`)
- Check key algorithm matches JWT header `alg` field

### "Token expired" with valid tokens

- Check clock synchronization between auth server and Titan (use NTP)
- Increase `clock_skew_seconds` if necessary (max 300s recommended)
- Verify token `exp` claim is in Unix timestamp format (seconds, not milliseconds)

### "Insufficient permissions" (403 Forbidden)

- Check JWT contains required `scope` or `roles` claim
- Verify claim values match route requirements
- Check `require_all_scopes` / `require_all_roles` logic matches your intent

## Next Steps

- **[Circuit Breaker Configuration](./circuit-breaker.md)** - Combine JWT with resilience patterns
- **[Configuration Overview](./overview.md)** - Complete configuration reference
- **[Deployment Guide](../deployment/overview.md)** - Deploy JWT-enabled Titan to production
