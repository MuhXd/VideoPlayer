#ifndef TEST_H
#define TEST_H
#include <Geode/Geode.hpp>
#include <Geode/ui/Layout.hpp>
#include <Geode/DefaultInclude.hpp>
#include "../include/VideoPlayer.hpp"

using namespace geode::prelude;

class TestLayer : public cocos2d::CCLayer
{
protected:
    bool init()
    {
        if (!cocos2d::CCLayer::init())
            return false;
        
        auto director = CCDirector::sharedDirector();
        auto winSize = director->getWinSize();
        auto background = createLayerBG();
        background->setID("background");
        this->addChild(background);

        auto menu = CCMenu::create();

        auto btn = CCMenuItemSpriteExtra::create(
            ButtonSprite::create("Test Formats"),
            this,
            menu_selector(TestLayer::Proceed));
        menu->addChild(btn);
        menu->setContentSize(winSize);
        menu->setAnchorPoint({0,0});
        menu->setPosition({10,10});
        menu->setID("test-menu");
        menu->setLayout(RowLayout::create()
                            ->setAxisAlignment(AxisAlignment::Start)
                            ->setCrossAxisLineAlignment(AxisAlignment::Start)
                            ->setCrossAxisAlignment(AxisAlignment::Start));
        btn->setID("test-videos");
        this->addChild(menu);

        CCSprite *backSpr = CCSprite::createWithSpriteFrameName("GJ_arrow_03_001.png");
        CCMenuItemSpriteExtra *backBtn = CCMenuItemSpriteExtra::create(backSpr, this, menu_selector(TestLayer::onClose));
        backBtn->setID("back-button");

        CCMenu *buttonMenu = CCMenu::create();
        buttonMenu->addChild(backBtn);
        this->addChild(buttonMenu);
        buttonMenu->setID("button-menu");

        backBtn->setPosition(ccp(-winSize.width / 2 + 23.f, winSize.height / 2 - 25.f));
        menu->updateLayout();

        GameManager::sharedState()->fadeInMusic("Kazumi Totaka - Shop Channel.mp3"_spr);
        
        setKeyboardEnabled(true);
        setKeypadEnabled(true);

        return true;
    }
    void onClose(CCObject *)
    {
        GameManager::sharedState()->fadeInMenuMusic();
        CCDirector::sharedDirector()->replaceScene(CCTransitionFadeTR::create(0.5f, MenuLayer::scene(false)));
    };
    void keyBackClicked() {
        onClose(nullptr);
    };
    void playVideo(std::filesystem::path const& path, std::function<void()> cb, bool loop = false) {
        if (!std::filesystem::exists(path)) {
            return;
        }
        std::filesystem::path fs = std::filesystem::path(path);
        CCLabelBMFont* text = CCLabelBMFont::create(fs.filename().extension().string().c_str(), "bigFont.fnt");
        text->setAnchorPoint({1,1});
        const std::function<void()> callback = std::move(cb);
        auto video = videoplayer::VideoPlayer::create(path,loop);
        
        video->onVideoEnd([=]{
            if (!loop) video->removeFromParentAndCleanup(true);
            if (callback) callback();
        });

        auto pos = CCDirector::sharedDirector();
        video->setPosition(pos->getWinSize() / 2);

        this->addChild(video);
        video->addChild(text);
    }
    void Proceed(int NextStage) {
        switch (NextStage) {
                case 1:
                playVideo(Mod::get()->getResourcesDir() / "Sample.avi",[=](){
                        Proceed(NextStage+1);
                });
                break;
          case 2:
                playVideo(Mod::get()->getResourcesDir() / "Sample.mov",[=](){
                        Proceed(NextStage+1);
                });
                break;
            case 3:
                playVideo(Mod::get()->getResourcesDir() / "Sample.mkv",[=](){
                        Proceed(NextStage+1);
                });
                break;
            default: break;
        }
    }
    void Proceed(CCObject *sender)
    {
       playVideo(Mod::get()->getResourcesDir() / "tennaIntroF1_compressed_28.mp4",[=](){
            Proceed(1);
       });
    };


public:
    static TestLayer *create()
    {
        auto ret = new TestLayer();
        if (ret->init())
        {
            ret->autorelease();
            return ret;
        }
        delete ret;
        return nullptr;
    };
    static cocos2d::CCScene *scene()
    {
        auto layer = TestLayer::create();
        auto scene = CCScene::create();
        scene->addChild(layer);
        return scene;
    };
};
#endif