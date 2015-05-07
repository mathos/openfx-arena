#include "ofxsImageEffect.h"
#include "MagickMirror.h"
#include "MagickTile.h"

namespace OFX 
{
  namespace Plugin 
  {
    void getPluginIDs(OFX::PluginFactoryArray &ids)
    {
        getMagickMirrorPluginID(ids);
        getMagickTilePluginID(ids);
    }
  }
}
