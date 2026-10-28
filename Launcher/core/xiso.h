#pragma once
#include "common.h"
// Extracts an Xbox / Xbox 360 disc image (XDVDFS: raw XISO, XGD1/2/3 layouts) into dest.
// Progress is mapped into [p0, p1].
bool extractXiso(const fs::path& iso, const fs::path& dest, const Hooks& h, float p0, float p1, std::string& err);
