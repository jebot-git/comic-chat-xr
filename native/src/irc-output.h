// SPDX-License-Identifier: MIT
#pragma once
#include "comic.h"
namespace comic {
// Human-readable server/channel output, independent of comic panel pagination.
std::string ircOutput(const IrcLine &);
} // namespace comic
