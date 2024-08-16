#include "CrossWindow/CrossWindow.h"
#include "CrossWindow/Graphics.h"
#include "Renderer.h"

void xmain(int argc, const char** argv)
{
    // CREATE A WINDOW
    xwin::EventQueue event_queue;
    xwin::Window window;

    xwin::WindowDesc window_desc;
    window_desc.name = "MainWindow";
    window_desc.title = "DirectX12 Sandbox";
    window_desc.visible = true;
    window_desc.width = 1280;
    window_desc.height = 720;
    // windowDesc.fullscreen = true;

    if (!window.create(window_desc, event_queue)) {
        return;
    }
    //  CREATE A RENDERER
    Anni::Renderer renderer(window);

    //  ENGINE LOOP
    bool is_running = true;
    std::vector<xwin::KeyboardData> keybord_data;
    while (is_running) {
        bool should_render = true;
        // ️ Update the event queue
        event_queue.update();

        keybord_data.clear();
        //  Iterate through that queue:
        while (!event_queue.empty()) {
            // Update Events
            const xwin::Event& event = event_queue.front();

            //if (event.type == xwin::EventType::Resize) {

            //    std::cout << "resize"
            //              << "\n";
            //    const xwin::ResizeData data = event.data.resize;
            //    renderer.OnReSize(data.width, data.height);
            //    should_render = false;
            //}
            if (event.type == xwin::EventType::Close) {

                std::cout << "window supposed to close"
                          << "\n";
                window.close();
                should_render = false;
                is_running = false;
            }
            if (event.type == xwin::EventType::Keyboard) {
                std::cout << "key pressed" << "\n";
                keybord_data.push_back(event.data.keyboard);
            }
            event_queue.pop();
        }

        if (should_render) {
            renderer.OnUpdateGlobal(keybord_data);
            renderer.OnRender();
        }
    }
}