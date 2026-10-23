#include "profile.h"
#include "../script.h"

namespace game::swa {

std::unique_ptr<auto_play> make_auto_play(const std::string &scenario, uint32_t seed, bool checks);

namespace {

const video_traits VIDEO{};
const registration REGISTERED{ { &PROFILE, &IO_PROFILE, &VIDEO, nullptr, nullptr, make_auto_play } };

}
}
