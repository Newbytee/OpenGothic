#include "lightgroup.h"

#include "bounds.h"
#include "graphics/rendererstorage.h"
#include "graphics/sceneglobals.h"
#include "utils/gthfont.h"
#include "utils/workers.h"
#include "utils/dbgpainter.h"

#include "world/world.h"

using namespace Tempest;

size_t LightGroup::LightBucket::alloc() {
  for(auto& i:updated)
    i = false;
  if(freeList.size()>0) {
    auto ret = freeList.back();
    freeList.pop_back();
    return ret;
    }
  data.emplace_back();
  light.emplace_back();
  return data.size()-1;
  }

void LightGroup::LightBucket::free(size_t id) {
  for(auto& i:updated)
    i = false;
  if(id+1==data.size()) {
    data.pop_back();
    light.pop_back();
    } else {
    freeList.push_back(id);
    }
  }


LightGroup::Light::Light(LightGroup::Light&& oth):owner(oth.owner), id(oth.id) {
  oth.owner = nullptr;
  }

LightGroup::Light::Light(LightGroup& owner, const ZenLoad::zCVobData& vob)
  :owner(&owner) {
  LightSource l;
  l.setPosition(Vec3(vob.position.x,vob.position.y,vob.position.z));

  if(vob.zCVobLight.dynamic.rangeAniScale.size()>0) {
    l.setRange(vob.zCVobLight.dynamic.rangeAniScale,vob.zCVobLight.range,vob.zCVobLight.dynamic.rangeAniFPS,vob.zCVobLight.dynamic.rangeAniSmooth);
    } else {
    l.setRange(vob.zCVobLight.range);
    }

  if(vob.zCVobLight.dynamic.colorAniList.size()>0) {
    l.setColor(vob.zCVobLight.dynamic.colorAniList,vob.zCVobLight.dynamic.colorAniListFPS,vob.zCVobLight.dynamic.colorAniSmooth);
    } else {
    l.setColor(vob.zCVobLight.color);
    }

  std::lock_guard<std::recursive_mutex> guard(owner.sync);
  id = owner.alloc(l.isDynamic());
  auto& ssbo = owner.get(id);
  ssbo.pos   = l.position();
  ssbo.range = l.range();
  ssbo.color = l.color();

  auto& data = owner.getL(id);
  data = std::move(l);
  }

LightGroup::Light::Light(LightGroup& owner)
  :owner(&owner) {
  std::lock_guard<std::recursive_mutex> guard(owner.sync);
  id = owner.alloc(true);
  }

LightGroup::Light::Light(World& owner, const ZenLoad::zCVobData& vob)
  :Light(owner.view()->sGlobal.lights,vob) {
  owner.view()->needToUpdateUbo = true;
  }

LightGroup::Light::Light(World& owner)
  :Light(owner.view()->sGlobal.lights) {
  owner.view()->needToUpdateUbo = true;
  }

LightGroup::Light& LightGroup::Light::operator =(LightGroup::Light&& other) {
  std::swap(owner,other.owner);
  std::swap(id,other.id);
  return *this;
  }

LightGroup::Light::~Light() {
  if(owner!=nullptr)
    owner->free(id);
  }

void LightGroup::Light::setPosition(float x, float y, float z) {
  setPosition(Vec3(x,y,z));
  }

void LightGroup::Light::setPosition(const Vec3& p) {
  if(owner==nullptr)
    return;
  auto& ssbo = owner->get(id);
  ssbo.pos = p;

  auto& data = owner->getL(id);
  data.setPosition(p);
  }

void LightGroup::Light::setRange(float r) {
  if(owner==nullptr)
    return;
  auto& ssbo = owner->get(id);
  ssbo.range = r;

  auto& data = owner->getL(id);
  data.setRange(r);
  }

void LightGroup::Light::setColor(const Vec3& c) {
  if(owner==nullptr)
    return;
  auto& ssbo = owner->get(id);
  ssbo.color = c;

  auto& data = owner->getL(id);
  data.setColor(c);
  }

LightGroup::LightGroup(const SceneGlobals& scene)
  :scene(scene) {
  auto& device = scene.storage.device;
  for(auto& u:uboBuf)
    u = device.ubo<Ubo>(nullptr,1);

  LightBucket* bucket[2] = {&bucketSt, &bucketDyn};
  for(auto b:bucket) {
    for(int i=0;i<Resources::MaxFramesInFlight;++i) {
      auto& u = b->ubo[i];
      u = device.uniforms(scene.storage.pLights.layout());
      }
    }

  static const uint16_t index[36] = {
    0, 1, 3, 3, 1, 2,
    1, 5, 2, 2, 5, 6,
    5, 4, 6, 6, 4, 7,
    4, 0, 7, 7, 0, 3,
    3, 2, 7, 7, 2, 6,
    4, 5, 0, 0, 5, 1
    };
  ibo = device.ibo(index,36);

  static const Vec3 v[8] = {
    {-1,-1,-1},
    { 1,-1,-1},
    { 1, 1,-1},
    {-1, 1,-1},

    {-1,-1, 1},
    { 1,-1, 1},
    { 1, 1, 1},
    {-1, 1, 1},
    };
  vbo = device.vbo(v,8);
  }

void LightGroup::dbgLights(DbgPainter& p) const {
  int cnt = 0;
  p.setBrush(Color(1,0,0,0.01f));
  //p.setBrush(Color(1,0,0,1.f));

  const LightBucket* bucket[2] = {&bucketSt, &bucketDyn};
  for(auto b:bucket) {
    for(auto& i:b->light) {
      float r  = i.range();
      auto  pt = i.position();
      Vec3 px[9] = {};
      px[0] = pt+Vec3(-r,-r,-r);
      px[1] = pt+Vec3( r,-r,-r);
      px[2] = pt+Vec3( r, r,-r);
      px[3] = pt+Vec3(-r, r,-r);
      px[4] = pt+Vec3(-r,-r, r);
      px[5] = pt+Vec3( r,-r, r);
      px[6] = pt+Vec3( r, r, r);
      px[7] = pt+Vec3(-r, r, r);
      px[8] = pt;

      for(auto& i:px) {
        p.mvp.project(i.x,i.y,i.z);
        i.x = (i.x+1.f)*0.5f;
        i.y = (i.y+1.f)*0.5f;
        }

      int x = int(px[8].x*float(p.w));
      int y = int(px[8].y*float(p.h));

      int x0 = x, x1 = x;
      int y0 = y, y1 = y;
      float z0=px[8].z, z1=px[8].z;

      for(auto& i:px) {
        int x = int(i.x*float(p.w));
        int y = int(i.y*float(p.h));
        x0 = std::min(x0, x);
        y0 = std::min(y0, y);
        x1 = std::max(x1, x);
        y1 = std::max(y1, y);
        z0 = std::min(z0, i.z);
        z1 = std::max(z1, i.z);
        }

      if(z1<0.f || z0>1.f)
        continue;
      if(x1<0 || x0>int(p.w))
        continue;
      if(y1<0 || y0>int(p.h))
        continue;

      cnt++;
      p.painter.drawRect(x0,y0,x1-x0,y1-y0);
      p.painter.drawRect(x0,y0,3,3);
      }
    }

  char  buf[250]={};
  std::snprintf(buf,sizeof(buf),"light count = %d",cnt);
  p.drawText(10,50,buf);
  }

void LightGroup::free(size_t id) {
  std::lock_guard<std::recursive_mutex> guard(sync);
  if(id & staticMask)
    bucketSt .free(id); else
    bucketDyn.free(id);
  }

LightGroup::LightSsbo& LightGroup::get(size_t id) {
  if(id & staticMask) {
    for(auto& i:bucketSt.updated)
      i = false;
    return bucketSt.data[id^staticMask];
    }

  for(auto& i:bucketDyn.updated)
    i = false;
  return bucketDyn.data[id];
  }

LightSource& LightGroup::getL(size_t id) {
  if(id & staticMask) {
    return bucketSt.light[id^staticMask];
    }
  return bucketDyn.light[id];
  }

void LightGroup::tick(uint64_t time) {
  for(size_t i=0; i<bucketDyn.light.size(); ++i) {
    auto& light = bucketDyn.light[i];
    light.update(time);

    auto& ssbo = bucketDyn.data[i];
    ssbo.pos   = light.position();
    ssbo.color = light.currentColor();
    ssbo.range = light.currentRange();

    for(auto& updated:bucketDyn.updated)
      updated = false;
    }
  }

void LightGroup::preFrameUpdate(uint8_t fId) {
  LightBucket* bucket[2] = {&bucketSt, &bucketDyn};
  for(auto b:bucket) {
    if(b->updated[fId])
      continue;
    b->updated[fId] = true;
    if(b->ssbo[fId].size()==b->data.size()*sizeof(b->data[0])) {
      b->ssbo[fId].update(b->data);
      } else {
      b->ssbo[fId] = scene.storage.device.ssbo(BufferHeap::Upload,b->data);
      b->ubo [fId].set(4,b->ssbo[fId]);
      }
    }

  Ubo ubo;
  ubo.mvp    = scene.viewProject();
  ubo.mvpInv = ubo.mvp;
  ubo.mvpInv.inverse();
  ubo.fr.make(ubo.mvp);
  uboBuf[fId].update(&ubo,0,1);
  }

void LightGroup::draw(Encoder<CommandBuffer>& cmd, uint8_t fId) {
  static bool light = true;
  if(!light)
    return;
  auto& p = scene.storage.pLights;
  if(bucketSt.data.size()>0) {
    cmd.setUniforms(p,bucketSt.ubo[fId]);
    cmd.draw(vbo,ibo, 0,ibo.size(), 0,bucketSt.data.size());
    }
  if(bucketDyn.data.size()>0) {
    cmd.setUniforms(p,bucketDyn.ubo[fId]);
    cmd.draw(vbo,ibo, 0,ibo.size(), 0,bucketDyn.data.size());
    }
  }

void LightGroup::setupUbo() {
  LightBucket* bucket[2] = {&bucketSt, &bucketDyn};
  for(auto b:bucket) {
    for(int i=0;i<Resources::MaxFramesInFlight;++i) {
      auto& u = b->ubo[i];
      u.set(0,*scene.gbufDiffuse,Sampler2d::nearest());
      u.set(1,*scene.gbufNormals,Sampler2d::nearest());
      u.set(2,*scene.gbufDepth,  Sampler2d::nearest());
      u.set(3,uboBuf[i]);
      }
    }
  }

size_t LightGroup::alloc(bool dynamic) {
  size_t ret = 0;
  if(dynamic) {
    ret = bucketDyn.alloc();
    } else {
    ret = bucketSt .alloc();
    ret |= staticMask;
    }
  return ret;
  }
