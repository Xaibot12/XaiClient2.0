#pragma once
#include <vector>
#include <map>
#include <string>
#include "Module.h"

class ClickGUI {
public:
    std::vector<Module*> modules;
    bool open = true;

    ClickGUI();
    ~ClickGUI();

    void RegisterModule(Module* module);
    bool Render();
    
    std::string GetCategoryName(CategoryType type);
};
