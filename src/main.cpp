#include <ftxui/component/app.hpp>
#include <ftxui/component/component.hpp>
#include <ftxui/dom/elements.hpp>

using namespace ftxui;

int main() {

    auto app = App::Fullscreen();


    // Secciones
    auto song = text("Cancion") | center | border| flex;
    auto queue = text("Cola") | center | border| flex;
    auto console = text("Consola") | center | border| flex;

    auto top = hbox({
        song | flex_grow_factor(2),
        queue | flex_grow_factor(1)
    });

    auto renderer = Renderer([&] {
        return vbox({
            top | flex_grow_factor(1),
            console | flex_grow_factor(2)
        });
    });

    app.Loop(renderer);

    return 0;
}