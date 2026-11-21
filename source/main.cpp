#include "studio/studio.hpp"
#include "ui/ui.hpp"

int main() {
    ui::window window;
    if (!window.open("citadel")) return 1;
    {
        studio::studio app(window);
        while (window.frame()) {
            app.draw();
            window.present();
        }
    }
    window.close();
    return 0;
}
