"""
Mock Backend Server for Titan Testing and Benchmarking
Unified FastAPI server with circuit breaker control endpoints
"""
import os
import time
from fastapi import FastAPI, Response
from fastapi.responses import JSONResponse, PlainTextResponse

app = FastAPI(title="Titan Mock Backend")

# Circuit breaker state
state = {
    "failing": False,
    "request_count": 0,
    "error_count": 0
}


@app.get("/")
async def root():
    """Root endpoint - JSON response for compatibility"""
    return JSONResponse({"message": "Hello from mock backend", "port": int(os.getenv("PORT", "3001"))})


@app.get("/health")
async def health():
    """Health check endpoint"""
    return JSONResponse({"status": "healthy", "port": int(os.getenv("PORT", "3001"))})


@app.get("/api")
@app.post("/api")
async def api():
    """Main API endpoint with circuit breaker simulation"""
    state["request_count"] += 1
    if state["failing"]:
        state["error_count"] += 1
        return JSONResponse({"error": "Backend is failing"}, status_code=500)
    return JSONResponse({"message": "Success", "backend": "mock"})


@app.get("/public")
async def public_endpoint():
    """Public endpoint for JWT testing (no auth required)"""
    return JSONResponse({"message": "Public content", "auth": "not_required"})


@app.get("/protected")
async def protected_endpoint():
    """Protected endpoint for JWT testing (auth required)"""
    return JSONResponse({"message": "Protected content", "auth": "required"})


@app.get("/api/users")
async def get_users():
    """User list endpoint for JWT authz testing (requires read:users scope)"""
    return JSONResponse({
        "users": [
            {"id": "1", "name": "Alice"},
            {"id": "2", "name": "Bob"}
        ]
    })


@app.get("/api/users/{user_id}")
async def get_user(user_id: str):
    """Simulated user lookup - returns string user_id for compatibility"""
    return JSONResponse({
        "id": user_id,
        "name": f"User {user_id}",
        "email": f"user{user_id}@example.com",
        "port": int(os.getenv("PORT", "3001"))
    })


@app.get("/api/posts")
async def get_posts():
    """Posts endpoint for JWT authz testing (requires read:posts or read:all scope)"""
    return JSONResponse({
        "posts": [
            {"id": "1", "title": "First Post"},
            {"id": "2", "title": "Second Post"}
        ]
    })


@app.delete("/api/admin/users")
async def delete_users():
    """Admin delete users endpoint (requires delete:users AND admin:access scopes)"""
    return JSONResponse({"message": "Users deleted", "admin": True})


@app.post("/api/admin/posts")
async def create_admin_post():
    """Admin create post endpoint (requires write:posts scope AND admin role)"""
    return JSONResponse({"message": "Post created by admin", "id": "new-post"})


@app.get("/admin/dashboard")
async def admin_dashboard():
    """Admin dashboard endpoint (requires admin role)"""
    return JSONResponse({"message": "Admin dashboard", "role": "admin"})


@app.get("/admin/settings")
async def admin_settings():
    """Admin settings endpoint (requires admin OR moderator role)"""
    return JSONResponse({"message": "Admin settings", "roles": ["admin", "moderator"]})


@app.post("/api/data")
async def post_data(response: Response):
    """Echo endpoint for POST testing"""
    response.status_code = 201
    return JSONResponse({"message": "Data received", "timestamp": time.time()})


@app.get("/slow")
async def slow_endpoint():
    """Intentionally slow endpoint for timeout testing"""
    time.sleep(2)
    return PlainTextResponse("Slow response")


@app.get("/large")
async def large_response():
    """Large response for throughput testing"""
    data = {"items": [{"id": i, "value": f"item_{i}"} for i in range(1000)]}
    return JSONResponse(data)


# Circuit breaker control endpoints
@app.post("/_control/fail")
async def control_fail():
    """Control endpoint: start returning errors"""
    state["failing"] = True
    state["error_count"] = 0
    return JSONResponse({"status": "now_failing"})


@app.post("/_control/succeed")
async def control_succeed():
    """Control endpoint: stop returning errors"""
    state["failing"] = False
    return JSONResponse({"status": "now_healthy"})


@app.get("/_control/stats")
async def control_stats():
    """Control endpoint: get backend stats"""
    return JSONResponse(state)


@app.post("/_control/reset")
async def control_reset():
    """Control endpoint: reset backend state"""
    state["failing"] = False
    state["request_count"] = 0
    state["error_count"] = 0
    return JSONResponse({"status": "reset"})


if __name__ == "__main__":
    import os
    import uvicorn

    port = int(os.getenv("PORT", "3000"))
    uvicorn.run(app, host="0.0.0.0", port=port, log_level="warning")
