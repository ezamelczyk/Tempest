#pragma once

#include <Tempest/SystemApi>
#include <Tempest/Widget>

namespace Tempest {

class VectorImage;

class Window : public Widget {
  public:
    Window();
    virtual ~Window();

    int w() const;
    int h() const;

  protected:
    virtual void render();
    void         dispatchPaintEvent(VectorImage &e);

    SystemApi::Window* hwnd() const { return id; }

  private:
    struct Impl:SystemApi::WindowCallback {
      Impl(Window* self):self(self){}
      void onRender(Tempest::SystemApi::Window*) {
        if(self->w()>0 && self->h()>0) {
          self->render();
          }
        }
      void onResize(Tempest::SystemApi::Window*,int32_t w,int32_t h){
        self->resize(w,h);
        }

      void onMouse(MouseEvent& e){
        switch(e.type()) {
          case Event::MouseDown: self->mouseDownEvent(e); break;
          case Event::MouseUp:   self->mouseUpEvent(e);   break;
          case Event::MouseMove: self->mouseMoveEvent(e); break;
          default:break;
          }
        }
      Window* self;
      };

    Impl               impl;
    SystemApi::Window* id=nullptr;
  };

}
