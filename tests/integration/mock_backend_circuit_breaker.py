"""
Mock backend server for circuit breaker integration testing
Supports failure injection via control API
"""
import argparse
import sys
from flask import Flask, jsonify, request

app = Flask(__name__)

# State management
state = {
    "failing": False,  # Whether to return errors
    "request_count": 0,
    "error_count": 0,
}


@app.route("/health", methods=["GET"])
def health():
    """Health check endpoint"""
    state["request_count"] += 1
    if state["failing"]:
        state["error_count"] += 1
        return jsonify({"status": "unhealthy"}), 500
    return jsonify({"status": "healthy"}), 200


@app.route("/api", methods=["GET", "POST"])
def api():
    """Main API endpoint"""
    state["request_count"] += 1
    if state["failing"]:
        state["error_count"] += 1
        return jsonify({"error": "Backend is failing"}), 500
    return jsonify({"message": "Success", "backend": "mock"}), 200


@app.route("/_control/fail", methods=["POST"])
def control_fail():
    """Control endpoint: start returning errors"""
    state["failing"] = True
    state["error_count"] = 0
    return jsonify({"status": "now_failing"}), 200


@app.route("/_control/succeed", methods=["POST"])
def control_succeed():
    """Control endpoint: stop returning errors"""
    state["failing"] = False
    return jsonify({"status": "now_healthy"}), 200


@app.route("/_control/stats", methods=["GET"])
def control_stats():
    """Control endpoint: get backend stats"""
    return jsonify(state), 200


@app.route("/_control/reset", methods=["POST"])
def control_reset():
    """Control endpoint: reset stats"""
    state["failing"] = False
    state["request_count"] = 0
    state["error_count"] = 0
    return jsonify({"status": "reset"}), 200


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Mock backend for circuit breaker testing")
    parser.add_argument("--port", type=int, default=3001, help="Port to listen on")
    args = parser.parse_args()

    print(f"Starting mock backend on port {args.port}")
    app.run(host="127.0.0.1", port=args.port, debug=False)
