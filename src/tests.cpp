#include <Geode/Geode.hpp>
#include <Geode/modify/MenuLayer.hpp>
#include "test.hpp"
using namespace geode::prelude;
class $modify(MainMenuHookTests, MenuLayer)
{

	bool init()
	{
		if (!MenuLayer::init())
			return false;
        
        auto CCMenuItemButton = CCMenuItemExt::createSpriteExtra(ButtonSprite::create("Test layer"), [](auto selector){
			CCDirector::sharedDirector()->replaceScene(CCTransitionFadeTR::create(0.5f, TestLayer::scene()));
		});
        CCMenuItemButton->setID("test"_spr);
        if (auto l = this->getChildByID("bottom-menu")) {
            l->addChild(CCMenuItemButton);
            l->updateLayout();
        };
        log::debug("inited");

        return true;
    };
};
