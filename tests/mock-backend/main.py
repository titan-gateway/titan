"""
Mock Backend Server for Titan Testing and Benchmarking
Simple FastAPI server that responds to various endpoints
"""
from fastapi import FastAPI, Response
from fastapi.responses import JSONResponse, PlainTextResponse
import time

app = FastAPI(title="Titan Mock Backend")


@app.get("/")
async def root():
    """Root endpoint - simple text response"""
    return PlainTextResponse("Mock Backend v1.0")


@app.get("/health")
async def health():
    """Health check endpoint"""
    return JSONResponse({"status": "healthy", "timestamp": time.time()})


@app.get("/api/users/{user_id}")
async def get_user(user_id: int):
    """Simulated user lookup"""
    return JSONResponse({
        "id": user_id,
        "name": f"User {user_id}",
        "email": f"user{user_id}@example.com"
    })


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


if __name__ == "__main__":
    import os
    import uvicorn

    port = int(os.getenv("PORT", "3000"))
    uvicorn.run(app, host="0.0.0.0", port=port, log_level="warning")
