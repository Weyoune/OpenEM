// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// Match reporting mode of a team (team record +0x34 bits 6..7): how its matches are reported,
// including the limit of four televised ("camera") teams.
#include "formats/hok.hpp"
namespace em {
// Sets the reporting mode (0..2) of `team` (0..149) and returns the mode actually set. Mode 2 is
// the camera mode: national teams (52..63) and all other teams are counted separately, and when the
// camera teams already set in the group reach four the request is resolved as in the original: for a team
// without manager the request falls back to mode 0, for a manager's team the first earlier camera
// team without a manager is demoted to mode 1. Throws FormatError for invalid arguments.
int set_team_reporting(HokFile&,int team,int mode);
}
