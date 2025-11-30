"""
Simple mock backend server for integration testing
"""
import os
import sys
from flask import Flask, jsonify, request

app = Flask(__name__)

# Get port from environment variable
PORT = int(os.environ.get("PORT", 3001))


@app.route("/health", methods=["GET"])
def health():
    """Health check endpoint"""
    return jsonify({"status": "healthy", "port": PORT}), 200


@app.route("/", methods=["GET"])
def root():
    """Root endpoint"""
    return jsonify({"message": "Hello from mock backend", "port": PORT}), 200


@app.route("/api", methods=["GET"])
def api():
    """Generic API endpoint"""
    return jsonify({"message": "API endpoint", "port": PORT}), 200


@app.route("/api/users/<user_id>", methods=["GET"])
def get_user(user_id):
    """Get user by ID"""
    return jsonify({"id": user_id, "name": f"User {user_id}", "port": PORT}), 200


@app.route("/large", methods=["GET"])
def large():
    """Return a large response"""
    data = "x" * 10000
    return jsonify({"data": data, "size": len(data), "port": PORT}), 200


@app.route("/slow", methods=["GET"])
def slow():
    """Slow endpoint (simulates latency)"""
    import time
    time.sleep(0.5)
    return jsonify({"message": "Slow response", "port": PORT}), 200


if __name__ == "__main__":
    print(f"Starting mock backend on port {PORT}")
    sys.stdout.flush()
    app.run(host="127.0.0.1", port=PORT, debug=False, threaded=True)
