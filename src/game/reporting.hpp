// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "formats/hok.hpp"
namespace em {
//1902:000B: original report/camera selection and four-camera arbitration.
int set_team_reporting(HokFile&,int team,int mode);
}
