#include <Tempest/Application>

#include <Tempest/VulkanApi>
#include <Tempest/DirectX12Api>
#include <Tempest/MetalApi>

#include <Tempest/Device>

#include "game.h"

std::unique_ptr<Tempest::AbstractGraphicsApi> mkApi(const char* av) {
#if defined(__OSX__)
  (void)av;
  return std::unique_ptr<Tempest::AbstractGraphicsApi>(new Tempest::MetalApi{Tempest::ApiFlags::Validation});
#else
  if(std::strcmp(av,"dx12")==0)
    return std::unique_ptr<Tempest::AbstractGraphicsApi>(new Tempest::DirectX12Api{Tempest::ApiFlags::Validation});
  return std::unique_ptr<Tempest::AbstractGraphicsApi>(new Tempest::VulkanApi{Tempest::ApiFlags::Validation});
#endif
  }

int main(int argc,const char** argv) {
  Tempest::Application app;
  auto api = mkApi(argc>1 ? argv[1] : "");

  Tempest::Device device{*api};
  Game            wx(device);

  return app.exec();
  }
