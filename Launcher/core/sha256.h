#pragma once
#include "common.h"
// Returns lowercase hex, or "" on open failure / cancel.
std::string sha256File(const fs::path& p, const std::function<bool()>& cancelled = {});
