#pragma once

#include <map>
#include <string>
#include <vector>

enum class ParamType {
  Int,
  Double,
  Bool,
  String,
};

struct QueryParam {
  std::string name;
  ParamType type = ParamType::String;
  bool required = false;
};

struct RouteEntry {
  std::string host;
  std::vector<std::string> path;
  std::string routeId;
  std::vector<QueryParam> query;
};

struct TypedParam {
  ParamType type = ParamType::String;
  long long intValue = 0;
  double doubleValue = 0.0;
  bool boolValue = false;
  std::string stringValue;
};

enum class MatchError {
  NONE,
  NO_MATCH,
  TYPE_MISMATCH,
  MISSING_PARAM,
};

struct MatchResult {
  bool matched = false;
  std::string routeId;
  std::map<std::string, TypedParam> args;
  MatchError error = MatchError::NO_MATCH;
};

// Compiled route-manifest trie (cold path, no JS). The manifest table is
// copied at construction; match() resolves host + path segments (":name"
// captures a String path param) then coerces declared query params.
class RouteTrie {
 public:
  explicit RouteTrie(std::vector<RouteEntry> routes);

  MatchResult match(const std::string& host,
                    const std::vector<std::string>& path,
                    const std::map<std::string, std::string>& query) const;

 private:
  std::vector<RouteEntry> routes_;
};
