#!/usr/bin/env python3
"""Reject mismatched release requests before starting the build matrix."""
import argparse
import os
import re


def check(channel, xweather, workflow_id, branch, repository):
    if channel not in ("production", "xweather-alpha"):
        raise ValueError("Unknown publication channel")
    if xweather != (channel == "xweather-alpha"):
        raise ValueError("Production requires build_xweather_identity=false; xweather-alpha requires true")
    if workflow_id and not re.fullmatch(r"[0-9a-fA-F-]{36}", workflow_id):
        raise ValueError("release_workflow_id must be a CircleCI workflow UUID")
    if channel == "production" and (branch != "master" or repository != "weather_routing_pi"):
        raise ValueError("Production publication must be triggered from WeatherRouting master")
    destination = ("opencpn/weather-routing-prod" if channel == "production" else
                   "pob220/xweather-routing-alpha-oss")
    print(f"Release request: {channel} -> {destination}; " +
          (f"reuse qualified workflow {workflow_id}" if workflow_id else "build and qualify both API matrices"))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--channel", required=True)
    parser.add_argument("--xweather", choices=("true", "false"), required=True)
    parser.add_argument("--workflow-id", default="")
    args = parser.parse_args()
    check(args.channel, args.xweather == "true", args.workflow_id,
          os.environ.get("CIRCLE_BRANCH", ""), os.environ.get("CIRCLE_PROJECT_REPONAME", ""))
