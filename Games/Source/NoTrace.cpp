#include "NoTrace.h"
#include "Config/NoTraceConfig.h"
#include "Config/NoTraceGameplayConfig.h"
#include "Config/NoTraceMusicConfig.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace NoTrace {
  namespace {
    tgui::Label::Ptr AddLabel(const tgui::Group::Ptr &parent, const tgui::String &text,
                             const tgui::Layout &left, const tgui::Layout &top,
                             const tgui::Layout &width, unsigned int textSize,
                             const tgui::Color &color, const tgui::Layout &height = "5%") {
      auto label = tgui::Label::create(text);
      label->setSize(width, height);
      label->setPosition(left, top);
      label->setTextSize(textSize);
      label->setVerticalAlignment(tgui::VerticalAlignment::Center);
      label->getRenderer()->setTextColor(color);
      label->setIgnoreMouseEvents(true);
      parent->add(label);
      return label;
    }

    tgui::Button::Ptr AddButton(EngineContext &ctx, const tgui::Group::Ptr &parent,
                               const tgui::String &text, const tgui::Layout &left,
                               const tgui::Layout &top, const tgui::Layout &width = "34%") {
      auto button = tgui::Button::create(text);
      button->setSize(width, "7%");
      button->setPosition(left, top);
      button->setTextSize(BUTTON_TEXT_SIZE);
      button->setTextPosition({"8%", "50%"}, {0, 0.5f});
      auto renderer = button->getRenderer();
      renderer->setBorders({3, 1, 1, 1});
      renderer->setBorderColor({69, 83, 99});
      renderer->setBorderColorHover(ACCENT_COLOR);
      renderer->setBorderColorFocused(ACCENT_COLOR);
      renderer->setBackgroundColor({6, 11, 19, 135});
      renderer->setBackgroundColorHover({58, 16, 25, 220});
      renderer->setBackgroundColorFocused({43, 13, 22, 210});
      renderer->setTextColor(TEXT_COLOR);
      renderer->setTextColorHover(tgui::Color::White);
      renderer->setTextColorFocused(tgui::Color::White);
      button->onMouseEnter([&ctx] { ctx.cursor.SetHovered(true); });
      button->onMouseLeave([&ctx] { ctx.cursor.SetHovered(false); });
      parent->add(button);
      return button;
    }

    tgui::Group::Ptr CreateScreen(bool subtitle = true) {
      auto root = tgui::Group::create();
      if (subtitle) AddLabel(root, "CIO  //  CLANDESTINE INTELLIGENCE ORGANIZATION", "5.5%", "28.5%", "57%",
                             SMALL_TEXT_SIZE, MUTED_COLOR);
      AddLabel(root, "v0.1.1   |   Carlos Cortina   |   CIO", "5.5%", "94%", "43%",
               SMALL_TEXT_SIZE, MUTED_COLOR);
      auto slogan = AddLabel(root, "INFILTRA  ·  CUMPLE  ·  DESAPARECE", "65%", "94%", "31%",
                             SMALL_TEXT_SIZE, TEXT_COLOR);
      slogan->setHorizontalAlignment(tgui::HorizontalAlignment::Right);
      return root;
    }

    void PrepareScreen(EngineContext &ctx, const tgui::Group::Ptr &root) {
      ctx.renderer.ResetView();
      ctx.renderer.SetPostProcessingEnabled(false);
      ctx.renderer.SetTextureSmoothing(false);
      ctx.cursor.SetVisible(true);
      ctx.cursor.SetHovered(false);
      root->setEnabled(true);
      ctx.gui.Add(root);
    }

  }

  Menu::Menu(EngineContext &context) : Scene(context), backdrop_(context), root_(CreateScreen(false)) {
    mainPage_ = tgui::Group::create();
    optionsPage_ = tgui::Group::create();
    root_->add(mainPage_);
    root_->add(optionsPage_);
    InitMainPage();
    InitOptionsPage();
  }

  void Menu::InitMainPage() {
    AddLabel(mainPage_, "CIO  //  CLANDESTINE INTELLIGENCE ORGANIZATION", "5.5%", "28.5%", "57%", SMALL_TEXT_SIZE, MUTED_COLOR);
    mainFocus_ = AddButton(ctx, mainPage_, "NUEVA OPERACIÓN", "5.5%", "43%");
    mainFocus_->onPress([this] { ctx.scenes.ChangeScene(std::string(GameplayConfig::GameScene)); });
    AddButton(ctx, mainPage_, "OPCIONES", "5.5%", "54%")->onPress([this] { ShowPage(MenuPage::Options); });
    AddButton(ctx, mainPage_, "SALIR", "5.5%", "65%")->onPress([this] { ctx.scenes.RequestQuit(); });
    AddLabel(mainPage_, "OPERACIONES CLANDESTINAS", "5.5%", "78%", "40%", SMALL_TEXT_SIZE, MUTED_COLOR);
    AddLabel(mainPage_, "Una entrada. Múltiples decisiones.", "5.5%", "82%", "45%", BODY_TEXT_SIZE, TEXT_COLOR);
  }

  void Menu::InitOptionsPage() {
    const auto panel = [&] {
      auto result = tgui::Panel::create({780.f, 596.f}); result->setPosition("4%", "25%");
      result->getRenderer()->setBackgroundColor({3, 8, 14, 250});
      result->getRenderer()->setBorderColor(ACCENT_COLOR); optionsPage_->add(result); return result;
    };
    optionsPanel_ = panel(); controlsPanel_ = panel();
    options_ = BuildOptionsUi(ctx, optionsPanel_, [this] { ShowPage(MenuPage::Controls); },
                             [this] { ShowPage(MenuPage::Main); }, "VOLVER");
    resolutionBox_ = options_.resolution; modeBox_ = options_.mode;
    volumeSlider_ = options_.masterVolume; cursorSlider_ = options_.cursorSpeed;
  }

  void Menu::ShowPage(MenuPage page) {
    page_ = page;
    ctx.cursor.SetHovered(false);
    mainPage_->setVisible(page_ == MenuPage::Main);
    optionsPage_->setVisible(page_ != MenuPage::Main);
    optionsPanel_->setVisible(page_ == MenuPage::Options);
    controlsPanel_->setVisible(page_ == MenuPage::Controls);
    if (page_ == MenuPage::Controls) {
      controlsPanel_->removeAllWidgets();
      BuildControlsUi(ctx, controlsPanel_, [this] { ShowPage(MenuPage::Options); },
                      [this] { ShowPage(MenuPage::Options); });
    }
    fade_ = 0.f;
    if (page_ == MenuPage::Options) {
      SyncDisplayOptions();
    }
    FocusPage();
  }

  void Menu::FocusPage() {
    if (page_ == MenuPage::Main) {
      mainFocus_->setFocused(true);
    } else if (page_ == MenuPage::Options) {
      resolutionBox_->setFocused(true);
    }
  }

  void Menu::SyncDisplayOptions() {
    observedDisplay_ = ctx.window.GetDisplaySettings(); SyncOptionsUi(ctx, options_);
  }

  void Menu::Start() {
    ApplyUserPreferences(ctx); PrepareScreen(ctx, root_); ShowPage(MenuPage::Main);
    ctx.audio.PlayMusic(ctx.resources, MusicConfig::Menu); ctx.audio.SetMusicIntensity(0.f);
    SyncDisplayOptions();
  }

  void Menu::Update() {
    const float deltaTime = ctx.time.GetDeltaTime();
    fade_ = std::min(fade_ + deltaTime / PAGE_FADE_DURATION, 1.f);
    (page_ == MenuPage::Main ? mainPage_ : optionsPage_)->getRenderer()->setOpacity(fade_);
    if (page_ == MenuPage::Options && observedDisplay_ != ctx.window.GetDisplaySettings()) {
      SyncDisplayOptions();
    }
  }

  void Menu::Render() const {
    backdrop_.Render();
  }

  void Menu::OnEvent(const sf::Event &event) {
    const auto *key = event.getIf<sf::Event::KeyPressed>();
    if (!key || page_ == MenuPage::Main) return;
    if (key->scancode == sf::Keyboard::Scan::Escape) {
      ShowPage(page_ == MenuPage::Controls ? MenuPage::Options : MenuPage::Main); return;
    }
    if (page_ == MenuPage::Options) HandleOptionsKey(event, options_);
  }

  void Menu::OnPause(bool paused) {
    root_->setEnabled(!paused);
    ctx.cursor.SetHovered(false);
    if (!paused) {
      FocusPage();
    }
  }

  void Menu::OnCleanup() {
    ctx.audio.StopMusic(MusicConfig::SceneFadeOutSeconds);
    ctx.cursor.SetHovered(false);
    ctx.renderer.SetPostProcessingEnabled(true);
    ctx.renderer.SetTextureSmoothing(true);
    ctx.gui.Remove(root_);
  }

  Reception::Reception(EngineContext &context) : Scene(context), backdrop_(context), root_(CreateScreen()) {
    auto panel = tgui::Panel::create();
    panel->setPosition("4%", "39%");
    panel->setSize("56%", "44%");
    panel->getRenderer()->setBackgroundColor({3, 8, 14, 235});
    root_->add(panel);
    AddLabel(root_, "OPERACIÓN EN PREPARACIÓN", "5.5%", "42%", "51%", HEADING_TEXT_SIZE, TEXT_COLOR);
    AddLabel(root_, "Bienvenido a CIO, agente.\n\nLa base y los contratos estarán disponibles\nen las próximas entregas.", "5.5%", "50%", "50%",
             BODY_TEXT_SIZE, MUTED_COLOR, "20%");
    backButton_ = AddButton(ctx, root_, "VOLVER AL MENÚ", "5.5%", "73%", "42%");
    backButton_->onPress([this] { ctx.scenes.ChangeScene(MENU_SCENE); });
  }

  void Reception::Start() {
    ApplyUserPreferences(ctx);
    PrepareScreen(ctx, root_);
    ctx.audio.PlayMusic(ctx.resources, MusicConfig::Lobby); ctx.audio.SetMusicIntensity(0.f);
    root_->getRenderer()->setOpacity(0.f);
    fade_ = 0.f;
    backButton_->setFocused(true);
  }

  void Reception::Update() {
    const float deltaTime = ctx.time.GetDeltaTime();
    fade_ = std::min(fade_ + deltaTime / PAGE_FADE_DURATION, 1.f);
    root_->getRenderer()->setOpacity(fade_);
  }

  void Reception::Render() const {
    backdrop_.Render();
  }

  void Reception::OnPause(bool paused) {
    root_->setEnabled(!paused);
    ctx.cursor.SetHovered(false);
    if (!paused) {
      backButton_->setFocused(true);
    }
  }

  void Reception::OnCleanup() {
    ctx.audio.StopMusic(MusicConfig::SceneFadeOutSeconds);
    ctx.cursor.SetHovered(false);
    ctx.renderer.SetPostProcessingEnabled(true);
    ctx.renderer.SetTextureSmoothing(true);
    ctx.gui.Remove(root_);
  }
}
