#include "NoTrace.h"
#include "Config/NoTraceConfig.h"
#include "Config/NoTraceGameplayConfig.h"

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

    tgui::Group::Ptr CreateScreen() {
      auto root = tgui::Group::create();
      AddLabel(root, "CIO  //  CLANDESTINE INTELLIGENCE ORGANIZATION", "5.5%", "28.5%", "57%",
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

    std::string ResolutionText(sf::Vector2u size) {
      return std::to_string(size.x) + " x " + std::to_string(size.y);
    }
  }

  Menu::Menu(EngineContext &context) : Scene(context), backdrop_(context), root_(CreateScreen()) {
    mainPage_ = tgui::Group::create();
    optionsPage_ = tgui::Group::create();
    root_->add(mainPage_);
    root_->add(optionsPage_);
    InitMainPage();
    InitOptionsPage();
  }

  void Menu::InitMainPage() {
    mainFocus_ = AddButton(ctx, mainPage_, "NUEVA OPERACIÓN", "5.5%", "43%");
    mainFocus_->onPress([this] { ctx.scenes.ChangeScene(std::string(GameplayConfig::GameScene)); });
    AddButton(ctx, mainPage_, "OPCIONES", "5.5%", "54%")->onPress([this] { ShowPage(MenuPage::Options); });
    AddButton(ctx, mainPage_, "SALIR", "5.5%", "65%")->onPress([this] { ctx.scenes.RequestQuit(); });
    AddLabel(mainPage_, "OPERACIONES CLANDESTINAS", "5.5%", "78%", "40%", SMALL_TEXT_SIZE, MUTED_COLOR);
    AddLabel(mainPage_, "Una entrada. Múltiples decisiones.", "5.5%", "82%", "45%", BODY_TEXT_SIZE, TEXT_COLOR);
  }

  void Menu::InitOptionsPage() {
    auto panel = tgui::Panel::create();
    panel->setPosition("4%", "33%");
    panel->setSize("61%", "59%");
    panel->getRenderer()->setBackgroundColor({3, 8, 14, 235});
    panel->getRenderer()->setBorderColor({49, 59, 76});
    panel->setIgnoreMouseEvents(true);
    optionsPage_->add(panel);
    AddLabel(optionsPage_, "OPCIONES", "5.5%", "34%", "36%", HEADING_TEXT_SIZE, TEXT_COLOR);
    currentLabel_ = AddLabel(optionsPage_, "", "5.5%", "39%", "56%", SMALL_TEXT_SIZE, MUTED_COLOR);

    AddLabel(optionsPage_, "Resolución", "5.5%", "45%", "23%", BODY_TEXT_SIZE, TEXT_COLOR);
    resolutionBox_ = tgui::ComboBox::create();
    resolutionBox_->setPosition("30%", "45%");
    resolutionBox_->setSize("29%", "5%");
    resolutionBox_->setTextSize(BODY_TEXT_SIZE);
    resolutionBox_->setItemsToDisplay(6);
    optionsPage_->add(resolutionBox_);

    AddLabel(optionsPage_, "Modo de pantalla", "5.5%", "52%", "23%", BODY_TEXT_SIZE, TEXT_COLOR);
    modeBox_ = tgui::ComboBox::create();
    modeBox_->setPosition("30%", "52%");
    modeBox_->setSize("29%", "5%");
    modeBox_->setTextSize(BODY_TEXT_SIZE);
    optionsPage_->add(modeBox_);

    volumeLabel_ = AddLabel(optionsPage_, "", "5.5%", "59%", "24%", BODY_TEXT_SIZE, TEXT_COLOR);
    volumeSlider_ = tgui::Slider::create(MIN_VOLUME, MAX_VOLUME);
    volumeSlider_->setPosition("30%", "60%");
    volumeSlider_->setSize("29%", "3%");
    volumeSlider_->setStep(VOLUME_STEP);
    volumeSlider_->setFocusable(true);
    volumeSlider_->onValueChange([this](float value) { SetVolume(value); });
    optionsPage_->add(volumeSlider_);

    cursorLabel_ = AddLabel(optionsPage_, "", "5.5%", "66%", "24%", BODY_TEXT_SIZE, TEXT_COLOR);
    cursorSlider_ = tgui::Slider::create(MIN_CURSOR_SPEED, MAX_CURSOR_SPEED);
    cursorSlider_->setPosition("30%", "67%");
    cursorSlider_->setSize("29%", "3%");
    cursorSlider_->setStep(CURSOR_SPEED_STEP);
    cursorSlider_->setFocusable(true);
    cursorSlider_->onValueChange([this](float value) { SetCursorSpeed(value); });
    optionsPage_->add(cursorSlider_);

    statusLabel_ = AddLabel(optionsPage_, "Aplica y confirma los cambios de pantalla.", "5.5%", "72%", "55%",
                            SMALL_TEXT_SIZE, MUTED_COLOR);
    AddButton(ctx, optionsPage_, "APLICAR", "5.5%", "78%", "23%")->onPress([this] { ApplyDisplayOptions(); });
    auto defaults = AddButton(ctx, optionsPage_, "PREDETERMINADOS", "30%", "78%", "29%");
    defaults->setTextSize(BODY_TEXT_SIZE);
    defaults->onPress([this] { RestoreDefaults(); });
    AddButton(ctx, optionsPage_, "VOLVER", "5.5%", "86%", "23%")->onPress([this] { ShowPage(MenuPage::Main); });
  }

  void Menu::ShowPage(MenuPage page) {
    page_ = page;
    ctx.cursor.SetHovered(false);
    mainPage_->setVisible(page_ == MenuPage::Main);
    optionsPage_->setVisible(page_ == MenuPage::Options);
    fade_ = 0.f;
    if (page_ == MenuPage::Options) {
      SyncDisplayOptions();
    }
    FocusPage();
  }

  void Menu::FocusPage() {
    if (page_ == MenuPage::Main) {
      mainFocus_->setFocused(true);
    } else {
      resolutionBox_->setFocused(true);
    }
  }

  void Menu::SyncDisplayOptions() {
    observedDisplay_ = ctx.window.GetDisplaySettings();
    const auto &modes = ctx.window.GetAvailableResolutions();
    resolutionBox_->removeAllItems();
    for (const auto size : modes) {
      resolutionBox_->addItem(ResolutionText(size));
    }
    modeBox_->removeAllItems();
    modeBox_->addItem("Ventana");
    if (ctx.window.IsFullscreenAvailable()) {
      modeBox_->addItem("Pantalla completa");
    } else {
      statusLabel_->setText("SFML no ofrece modos compatibles; se mantiene el tamaño de ventana actual.");
    }
    auto selected = std::find(modes.begin(), modes.end(), observedDisplay_.resolution);
    if (selected == modes.end()) {
      selected = std::find(modes.begin(), modes.end(), ctx.window.GetDefaultDisplaySettings().resolution);
    }
    if (selected != modes.end()) {
      resolutionBox_->setSelectedItemByIndex(static_cast<std::size_t>(selected - modes.begin()));
    }
    modeBox_->setSelectedItemByIndex(observedDisplay_.fullscreen ? 1 : 0);
    currentLabel_->setText("ACTUAL: " + ResolutionText(observedDisplay_.resolution) +
                          (observedDisplay_.fullscreen ? " / Pantalla completa" : " / Ventana"));
  }

  void Menu::ApplyDisplayOptions() {
    const int index = resolutionBox_->getSelectedItemIndex();
    const auto &modes = ctx.window.GetAvailableResolutions();
    if (index < 0 || static_cast<std::size_t>(index) >= modes.size()) {
      statusLabel_->setText("Selecciona una resolución compatible.");
      return;
    }
    const DisplaySettings settings{modes[static_cast<std::size_t>(index)], modeBox_->getSelectedItemIndex() == 1};
    if (settings == ctx.window.GetDisplaySettings()) {
      statusLabel_->setText("La configuración seleccionada ya está activa.");
    } else if (!ctx.window.RequestDisplaySettings(settings)) {
      statusLabel_->setText("No se puede aplicar este modo de pantalla.");
    } else {
      statusLabel_->setText("Confirma el cambio o espera la restauración automática.");
    }
  }

  void Menu::RestoreDefaults() {
    volumeSlider_->setValue(gConfig.globalVolume);
    cursorSlider_->setValue(gConfig.cursorSpeed);
    SetVolume(volumeSlider_->getValue());
    SetCursorSpeed(cursorSlider_->getValue());
    const auto settings = ctx.window.GetDefaultDisplaySettings();
    if (settings != ctx.window.GetDisplaySettings()) {
      ctx.window.RequestDisplaySettings(settings);
    }
    SyncDisplayOptions();
    statusLabel_->setText("Valores predeterminados restaurados; confirma el modo si cambia.");
  }

  void Menu::SetVolume(float volume) {
    const float value = std::clamp(volume, MIN_VOLUME, MAX_VOLUME);
    ctx.audio.SetGlobalVolume(value);
    ctx.save.Set(VOLUME_SAVE_KEY, value);
    volumeLabel_->setText("Volumen general: " + std::to_string(static_cast<int>(std::lround(value))) + "%");
  }

  void Menu::SetCursorSpeed(float speed) {
    const float value = std::clamp(speed, MIN_CURSOR_SPEED, MAX_CURSOR_SPEED);
    ctx.cursor.SetSpeed(value);
    ctx.save.Set(CURSOR_SPEED_SAVE_KEY, value);
    cursorLabel_->setText("Cursor / mando: " + std::to_string(static_cast<int>(value)));
  }

  void Menu::Start() {
    PrepareScreen(ctx, root_);
    ShowPage(MenuPage::Main);
    volumeSlider_->setValue(std::clamp(ctx.save.Get<float>(VOLUME_SAVE_KEY, gConfig.globalVolume), MIN_VOLUME, MAX_VOLUME));
    cursorSlider_->setValue(std::clamp(ctx.save.Get<float>(CURSOR_SPEED_SAVE_KEY, gConfig.cursorSpeed), MIN_CURSOR_SPEED, MAX_CURSOR_SPEED));
    SetVolume(volumeSlider_->getValue());
    SetCursorSpeed(cursorSlider_->getValue());
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
    if (!key || page_ != MenuPage::Options) {
      return;
    }
    const auto slider = volumeSlider_->isFocused() ? volumeSlider_ :
                        (cursorSlider_->isFocused() ? cursorSlider_ : nullptr);
    if (!slider) {
      return;
    }
    // Los deslizadores TGUI requieren adaptar las teclas desde la escena.
    switch (key->scancode) {
    case sf::Keyboard::Scan::Left:
      slider->setValue(slider->getValue() - slider->getStep());
      break;
    case sf::Keyboard::Scan::Right:
      slider->setValue(slider->getValue() + slider->getStep());
      break;
    case sf::Keyboard::Scan::Home:
      slider->setValue(slider->getMinimum());
      break;
    case sf::Keyboard::Scan::End:
      slider->setValue(slider->getMaximum());
      break;
    default:
      break;
    }
  }

  void Menu::OnPause(bool paused) {
    root_->setEnabled(!paused);
    ctx.cursor.SetHovered(false);
    if (!paused) {
      FocusPage();
    }
  }

  void Menu::OnCleanup() {
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
    PrepareScreen(ctx, root_);
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
    ctx.cursor.SetHovered(false);
    ctx.renderer.SetPostProcessingEnabled(true);
    ctx.renderer.SetTextureSmoothing(true);
    ctx.gui.Remove(root_);
  }
}
