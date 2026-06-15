#include "GuiApp.h"

#include "App.h"

int main(int, char**)
{
    kickcat::gui::GuiApp gui{"KickCAT EEPROM Editor"};
    if (not gui.valid())
    {
        return 1;
    }

    kickcat::eeprom_editor::App app;
    gui.run([&app]{ app.render(); });

    return 0;
}
