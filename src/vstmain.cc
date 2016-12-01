#include "vst.h"

extern "C" {

__attribute__ ((visibility ("default")))
AEffect* VSTPluginMain (audioMasterCallback audioMaster)
{
	if (!audioMaster (0, audioMasterVersion, 0, 0, 0, 0))
		return 0;

	VstPlugin* effect = instantiate_vst (audioMaster);
	if (!effect)
		return 0;

	return effect->get_effect ();
}

#if (!defined _WIN32 || defined _WIN64)
/* i686-w64-mingw32-g++ can't export main */
__attribute__ ((visibility ("default")))
AEffect* wrap (audioMasterCallback audioMaster) asm ("main");

AEffect* wrap (audioMasterCallback audioMaster)
{
	return VSTPluginMain (audioMaster);
}
#endif

}
