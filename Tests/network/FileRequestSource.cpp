// The legacy make engine cannot map a source outside this directory to the
// local object path. Compile the production implementation through this
// translation unit so the focused test still exercises the real source.
#include "../../Server/FileRequest.cpp"
