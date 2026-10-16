#pragma once

namespace render {

using GLProc = void (*)();
using GLProcLoader = GLProc (*)(const char* name);

}
