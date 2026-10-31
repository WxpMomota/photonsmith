#pragma once
// model_text.h -- modeling round 3, part 10: lettering in a model. `text "WORDS" [size H] [depth D] [font FILE] [at X Y Z] [turn AX AY AZ]` in a piece pulls the words out
// into a solid H metres tall (a capital letter) and D deep - a shop's sign, a title, a name on a door, a logo. The letters' outlines come from a TrueType font (FILE under
// film_assets/fonts, the system's fonts, or a path; Arial when none is named) and are extruded as the Scene Builder's 3D text is (scene_text.h); it stands on y 0, centred
// across. (Made in film_parse.cpp, which builds the font reader in privately.)

#include <string>

#include "cartoon_lib.h"

namespace cartoon {
namespace model {

// The words as a solid in the model's space (y up, standing on 0, centred on x, its front facing +z), or an empty mesh and `error`.
Mesh textSolid(const std::string &words, double height, double depth, const std::string &font, std::string &error);

}  // namespace model
}  // namespace cartoon
