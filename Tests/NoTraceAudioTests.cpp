#include "NoTraceSimulation.h"
#include "NoTraceVisibility.h"
#include "NoTraceAudioGeometry.h"
#include "NoTraceDistractionGeometry.h"
#include "Config/NoTraceAudioConfig.h"
#include "Config/NoTraceDevicesConfig.h"
#include "Managers/AudioManager.h"
#include "Managers/ResourceManager.h"
#include "NoTraceSpatialFixture.h"
#include <SFML/Audio/Sound.hpp>
#include <SFML/Audio/SoundBuffer.hpp>
#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <set>
#include <stdexcept>

namespace {
  int checks = 0;
  void Require(bool value, const char *message) { ++checks; if (!value) throw std::runtime_error(message); }

  void RecordedLibraryHasIndependentVariants(const std::filesystem::path &root) {
    using namespace NoTrace;
    std::ifstream manifestFile(root / "Content/Audio/NoTrace/manifest.json");
    const auto manifest = nlohmann::json::parse(manifestFile);
    std::set<std::uint64_t> hashes;
    std::set<std::string> files;
    std::size_t variants = 0;
    std::size_t activeFamilies = 0;
    for (std::size_t index = 0; index < AudioConfig::Profiles.size(); ++index) {
      const auto kind = static_cast<SoundKind>(index);
      const auto &profile = AudioConfig::Profile(kind);
      if (kind == SoundKind::Cabinet) {
        Require(profile.count == 0 && AudioConfig::File(kind).empty() && AudioConfig::VariantFile(kind, 43).empty(), "El slot legado retirado intenta resolver una grabación o divide por cero");
        Require(!std::filesystem::exists(root / "Content/Audio/NoTrace/Objects/Cabinet-01.wav"), "El paquete conserva el WAV legado sin emisor");
        continue;
      }
      ++activeFamilies;
      Require(profile.count > 0 && profile.count <= profile.variants.size(), "Una acción carece de grabaciones válidas");
      Require(AudioConfig::Pitch(kind) == 1.f, "Una variante se fabrica cambiando el tono de otra toma");
      if (AudioConfig::IsFootstep(kind)) Require(profile.count >= 4, "Un material o postura de pasos carece de variantes físicas");
      for (std::size_t variant = 0; variant < profile.count; ++variant) {
        ++variants;
        Require(files.insert(AudioConfig::VariantFile(kind, variant)).second, "Dos acciones o variantes remiten al mismo archivo");
        sf::SoundBuffer buffer;
        Require(buffer.loadFromFile(root / "Content/Audio" / AudioConfig::VariantFile(kind, variant)), "Falta una grabación nueva o no es decodificable");
        const float duration = buffer.getDuration().asSeconds();
        Require(profile.loop ? duration >= 15.f && duration <= 60.f : duration > .02f && duration < 5.f, "Duración incompatible con el efecto o ambiente");
        Require(buffer.getChannelCount() == (profile.loop ? 2u : 1u) && buffer.getSampleRate() == 48000, "Formato PCM de producción incorrecto");
        std::uint64_t hash = 1469598103934665603ull; int peak = 0;
        for (std::uint64_t sample = 0; sample < buffer.getSampleCount(); ++sample) {
          const int value = buffer.getSamples()[sample]; peak = std::max(peak, std::abs(value));
          hash = (hash ^ static_cast<std::uint16_t>(value)) * 1099511628211ull;
        }
        Require(peak > 80 && peak < 32440, "La exportación contiene silencio o saturación");
        Require(hashes.insert(hash).second, "Dos archivos nuevos duplican el mismo PCM");
        const auto entry = std::ranges::find_if(manifest.at("assets"), [&](const nlohmann::json &asset) { return asset.at("file").get<std::string>() == std::string(profile.variants[variant]); });
        Require(entry != manifest.at("assets").end(), "Falta la procedencia de una variante activa");
        const auto &source = entry->at("sourceInfo");
        Require(source.at("license") == "CC0-1.0" && source.at("sourceFileSha256").template get<std::string>().size() == 64 && entry->at("sha256").template get<std::string>().size() == 64,
          "Una grabación activa carece de licencia distribuible o hash de procedencia");
        Require(entry->at("clippedSamples") == 0 && source.at("sourceUrl").template get<std::string>().starts_with("https://"), "La procedencia o nivel exportado no es verificable");
      }
    }
    Require(variants == manifest.at("assets").size() && variants == 184 && activeFamilies == 91, "El catálogo y el paquete de184grabaciones/91familias activas divergen");
    Require(AudioConfig::File(SoundKind::StepHard) != AudioConfig::File(SoundKind::StepMetal) && AudioConfig::File(SoundKind::StepMetal) != AudioConfig::File(SoundKind::StepSoft), "Hormigón, metal y suelo blando comparten recurso");
    for (const auto bank : {AudioConfig::ConcreteSteps, AudioConfig::MetalSteps, AudioConfig::SoilSteps}) {
      Require(AudioConfig::Footstep(bank, Stance::Standing, false) == bank.walk && AudioConfig::Footstep(bank, Stance::Standing, true) == bank.run && AudioConfig::Footstep(bank, Stance::Crouched, true) == bank.crouch,
        "La selección de material confunde caminar, correr y moverse agachado");
      Require(AudioConfig::Gain(bank.run) > AudioConfig::Gain(bank.walk) && AudioConfig::Gain(bank.walk) > AudioConfig::Gain(bank.crouch), "Un banco no conserva la intensidad relativa por postura");
    }
  }

  void MixerUsesBusesPrioritiesAndStableLoops(const std::filesystem::path &root) {
    using namespace NoTrace;
    sf::SoundBuffer buffer;
    Require(buffer.loadFromFile(root / "Content/Audio" / AudioConfig::File(SoundKind::AmbientRoom)), "No se carga la toma larga de prueba del mezclador");
    AudioManager mixer;
    const float initialMaster = mixer.GetGlobalVolume();
    mixer.SetGlobalVolume(37.f); Require(mixer.GetGlobalVolume() == 37.f, "El volumen maestro no controla el listener real");
    auto effect = std::make_shared<sf::Sound>(buffer), ambience = std::make_shared<sf::Sound>(buffer), music = std::make_shared<sf::Sound>(buffer);
    mixer.RegisterEffect(effect, 80.f); mixer.RegisterSource(ambience, 60.f, AudioManager::Bus::Ambience); mixer.RegisterSource(music, 40.f, AudioManager::Bus::Music);
    mixer.SetEffectsVolume(25.f); mixer.SetAmbienceVolume(50.f); mixer.SetMusicVolume(75.f);
    Require(std::abs(effect->getVolume() - 20.f) < .01f && std::abs(ambience->getVolume() - 30.f) < .01f && std::abs(music->getVolume() - 30.f) < .01f, "Los buses no aplican sus ganancias independientes a fuentes reales");
    mixer.SetEffectsVolume(0.f);
    Require(effect->getVolume() == 0.f && std::abs(ambience->getVolume() - 30.f) < .01f && std::abs(music->getVolume() - 30.f) < .01f, "Silenciar efectos altera ambiente o música");
    mixer.SetVoiceLimit(2);
    AudioManager::PlaybackSettings low;
    low.bus = AudioManager::Bus::Ambience; low.priority = AudioManager::Priority::Background; low.loop = true; low.volume = 0.f; low.maximumInstances = 1;
    const auto first = mixer.Play(buffer, "room", low);
    Require(first != 0 && mixer.IsPlaying(first), "El gestor no conserva la voz ambiental");
    Require(mixer.Play(buffer, "room", low) == first && mixer.ActiveVoices() == 1, "Actualizar un ambiente reinicia o duplica su bucle");
    AudioManager::PlaybackSettings high;
    high.priority = AudioManager::Priority::Critical; high.loop = true; high.volume = 0.f; high.maximumInstances = 1;
    const auto alarm = mixer.Play(buffer, "alarm", high);
    Require(alarm != 0 && mixer.ActiveVoices() == 2, "No se reservan dos voces reales");
    const auto secondAlarm = mixer.Play(buffer, "detection", high);
    Require(secondAlarm != 0 && mixer.IsPlaying(alarm) && !mixer.IsPlaying(first) && mixer.ActiveVoices() == 2, "Saturar canales corta una alerta antes que el ambiente");
    Require(mixer.Play(buffer, "new-room", low) == 0 && mixer.IsPlaying(alarm) && mixer.IsPlaying(secondAlarm), "Un Foley de baja prioridad interrumpe dos alertas críticas");
    Require(mixer.SetVoiceVolume(alarm, 12.f) && mixer.Play(buffer, "alarm", high) == alarm, "El ajuste de volumen reemplaza una voz activa");
    mixer.StopGroup("alarm"); Require(!mixer.IsPlaying(alarm) && mixer.IsPlaying(secondAlarm), "Parar un grupo afecta a otra alerta");
    mixer.StopAll(); Require(mixer.ActiveVoices() == 0, "La limpieza deja voces en reproducción");
    high.loop = false;
    Require(mixer.Play(buffer, "single", high) != 0 && mixer.Play(buffer, "single", high) == 0, "El límite de instancias permite duplicados del mismo evento");
    mixer.StopAll(); mixer.SetGlobalVolume(initialMaster);
  }

  void MissingResourceCanBeRetried(const std::filesystem::path &root) {
    const auto previous = std::filesystem::current_path();
    const auto source = std::filesystem::absolute(root / "Content/Audio" / NoTrace::AudioConfig::File(NoTrace::SoundKind::Button));
    const auto directory = previous / "build/qa-audio-resource-retry";
    std::filesystem::create_directories(directory / "Content/Audio/NoTrace");
    const auto file = directory / "Content/Audio/NoTrace/retry.wav";
    std::filesystem::remove(file);
    struct RestoreDirectory { std::filesystem::path previous; ~RestoreDirectory() { std::filesystem::current_path(previous); } } restore{previous};
    std::filesystem::current_path(directory);
    ResourceManager resources;
    Require(resources.FetchSound("NoTrace/retry.wav") == nullptr, "Una ausencia se anuncia como carga correcta");
    std::filesystem::copy_file(source, file);
    const auto buffer = resources.FetchSound("NoTrace/retry.wav");
    Require(buffer && buffer->getSampleCount() > 0 && resources.FetchSound("NoTrace/retry.wav") == buffer, "Un fallo de carga envenena la caché e impide el reintento posterior");
    std::filesystem::remove(file);
  }

  void ContactFoleyRequiresAPhysicalResult() {
    using namespace NoTrace;
    const auto prepare = [](Simulation &simulation) -> WorldState & {
      simulation.ResetBase();
      auto &world = const_cast<WorldState &>(simulation.GetState());
      world.guards.clear(); world.objects.clear(); world.obstacles.clear(); world.noises.clear(); world.sounds.clear(); world.securityEntries.clear();
      TestFixture::LegacyPlane(world);
      auto &agent = world.agents[0]; agent.position = {300.f, 300.f}; agent.facing = {1.f, 0.f};
      return world;
    };
    const auto strike = [](Simulation &simulation) {
      ActionCommand press; press.cqc = true; press.cqcHeld = true;
      simulation.Step(.1f, {{1, press}}); simulation.Step(.1f, {});
      for (int frame = 0; frame < 3; ++frame) simulation.Step(.1f, {});
    };
    const auto impacts = [](const WorldState &world) { return std::ranges::count_if(world.sounds, [](const SoundEvent &event) { return event.kind == SoundKind::Punch || event.kind == SoundKind::Kick; }); };
    Simulation simulation;
    auto &empty = prepare(simulation); strike(simulation);
    Require(impacts(empty) == 0 && !empty.noises.empty(), "Golpear el aire reproduce contacto corporal o modifica su comportamiento acústico de IA");
    auto &contact = prepare(simulation);
    Guard dummy; dummy.id = 81; dummy.trainingDummy = true; dummy.position = {328.f, 300.f}; dummy.facing = {-1.f, 0.f};
    contact.guards.push_back(dummy); strike(simulation);
    Require(impacts(contact) == 1 && contact.guards[0].trainingHits == 1 && contact.sounds.back().position == dummy.position,
      "Un contacto real no emite exactamente un impacto desde el cuerpo alcanzado");
    auto &blocked = prepare(simulation); blocked.guards.push_back(dummy);
    blocked.obstacles.push_back({{{314.f, 240.f}, {2.f, 120.f}}, CoverKind::High, true, "Pared entre atacante y diana"});
    strike(simulation);
    Require(impacts(blocked) == 0 && blocked.guards[0].trainingHits == 0, "Una pared impide el golpe pero deja sonar un impacto corporal");
    auto &pushed = prepare(simulation); dummy.facing = {1.f, 0.f}; pushed.guards.push_back(dummy);
    ActionCommand push; push.push = true; simulation.Step(.1f, {{1, push}});
    Require(pushed.guards[0].position.x > dummy.position.x && std::ranges::count(pushed.sounds, SoundKind::Push, &SoundEvent::kind) == 1,
      "Un empujón físico no produce su propia señal de contacto");
    simulation.Step(.1f, {{1, push}});
    Require(std::ranges::count(pushed.sounds, SoundKind::Push, &SoundEvent::kind) == 1, "Repetir V durante el cooldown reproduce un empujón inexistente");
    auto &immobile = prepare(simulation); immobile.guards.push_back(dummy);
    immobile.obstacles.push_back({{{342.f, 240.f}, {16.f, 120.f}}, CoverKind::High, true, "Pared que impide desplazar la diana"});
    simulation.Step(.1f, {{1, push}});
    Require(immobile.guards[0].position == dummy.position && std::ranges::none_of(immobile.sounds, [](const SoundEvent &event) { return event.kind == SoundKind::Push; }),
      "Un empujón bloqueado sin desplazamiento reproduce contacto consumado");
  }

  void EmptyTriggerHasFeedbackWithoutAProjectileOrSpam() {
    using namespace NoTrace;
    for (const bool lethal : {true, false}) for (const bool noReserve : {true, false}) {
      Simulation simulation; simulation.ResetBase(); simulation.Deploy(0);
      auto &world = const_cast<WorldState &>(simulation.GetState());
      world.objects.clear(); world.obstacles.clear(); world.guards.clear(); world.securityEntries.clear(); world.sounds.clear(); world.noises.clear(); world.events.clear();
      TestFixture::LegacyPlane(world);
      auto &agent = world.agents[0]; agent.position = {300.f, 300.f};
      int &ammo = lethal ? agent.loadout.lethalAmmo : agent.loadout.nonLethalAmmo;
      int &magazine = lethal ? agent.lethalMagazine : agent.nonLethalMagazine;
      ammo = noReserve ? 0 : 9; magazine = 0;
      ActionCommand trigger; trigger.fireLethal = lethal; trigger.fireNonLethal = !lethal; trigger.aimX = 1.f;
      simulation.Step(.05f, {{1, trigger}});
      Require(world.sounds.size() == 1 && world.sounds[0].kind == SoundKind::DryFire && world.sounds[0].actor == agent.id && agent.actionCooldown > 0.f,
        "Un gatillo sin reserva o cargador carece de contacto mecánico o enfriamiento autoritativo");
      const auto sequence = world.sounds[0].sequence;
      for (int frame = 0; frame < 5; ++frame) simulation.Step(.1f, {{1, trigger}});
      Require(world.sounds.size() == 1 && world.sounds[0].sequence == sequence, "Mantener el gatillo vacío reproduce un sonido por tick");
      for (int frame = 0; frame < 2; ++frame) simulation.Step(.1f, {{1, trigger}});
      Require(world.sounds.size() == 2 && world.sounds.back().kind == SoundKind::DryFire && world.sounds.back().sequence > sequence,
        "El gatillo vacío no vuelve a producir contacto al terminar su enfriamiento");
      Require(ammo == (noReserve ? 0 : 9) && magazine == 0 && world.shots.empty() && world.noises.empty() &&
        std::ranges::none_of(world.events, [](const MissionEvent &event) { return event.kind == EventKind::WeaponLethal || event.kind == EventKind::WeaponNonLethal; }),
        "El feedback de vacío consume munición, crea proyectiles, ruido de IA o registra un disparo inexistente");
    }
  }

  NoTrace::WorldState &AcousticFloors(NoTrace::Simulation &simulation) {
    using namespace NoTrace;
    simulation.ResetBase(); simulation.Deploy(0);
    auto &world = const_cast<WorldState &>(simulation.GetState());
    world.objects.clear(); world.obstacles.clear(); world.guards.clear(); world.securityEntries.clear(); world.sounds.clear(); world.noises.clear(); world.events.clear();
    TestFixture::LegacyPlane(world);
    world.walkSurfaces = {{1, 0, WalkSurfaceKind::Floor, {{0.f, 0.f}, world.size}, 0.f}, {2, 1, WalkSurfaceKind::Floor, {{100.f, 100.f}, {600.f, 600.f}}, 96.f}};
    world.agents[0].position = {300.f, 300.f}; world.agents[0].elevation = 96.f; world.agents[0].supportId = 2;
    return world;
  }

  void FootstepHearingUsesThePhysicalFloor() {
    using namespace NoTrace;
    for (const bool sameFloor : {false, true}) {
      Simulation simulation; auto &world = AcousticFloors(simulation);
      Guard guard; guard.id = 81; guard.position = {300.f, 330.f}; guard.facing = {0.f, 1.f};
      guard.elevation = sameFloor ? 96.f : 0.f; guard.supportId = sameFloor ? 2 : 1; world.guards.push_back(guard);
      world.agents[0].footstepTime = GameplayConfig::FootstepInterval;
      ActionCommand walk; walk.moveX = 1.f; simulation.Step(.1f, {{1, walk}});
      Require(world.noises.size() == 1 && world.noises[0].elevation == 96.f && world.noises[0].radius == GameplayConfig::WalkingNoiseRadius,
        "El paso pierde su planta física o altera el radio existente");
      Require(sameFloor ? world.guards[0].state == GuardState::Investigate && world.guards[0].lastKnownElevation == 96.f : world.guards[0].state == GuardState::Patrol,
        "Un paso arriba alerta al guardia fuera del radio 3D o el mismo paso deja de oírse en su planta");
      Require(!world.alarm && !world.agents[0].detectionConfirmed, "La pista acústica entre plantas se convierte en confirmación visual");
    }
  }

  void DecoyFoleyTracksPreparationAndAcceptedThrows() {
    using namespace NoTrace;
    Simulation simulation, replica; auto &world = AcousticFloors(simulation);
    world.decoys.clear(); auto &agent = world.agents[0]; agent.loadout.decoys = 3;
    const auto count = [&world](SoundKind kind) { return std::ranges::count(world.sounds, kind, &SoundEvent::kind); };
    ActionCommand prepare; prepare.prepareDecoy = true; prepare.aimX = 1.f; prepare.decoyDistance = 160.f;
    simulation.Step(.05f, {{agent.id, prepare}});
    Require(agent.decoyGuide.active && count(SoundKind::DecoyPrepare) == 1 && world.noises.empty() && world.decoys.empty() && agent.loadout.decoys == 3,
      "Preparar el señuelo no produce su contacto único o altera inventario, vuelo y ruido de IA");
    const auto sequence = world.sounds[0].sequence;
    for (int frame = 0; frame < 3; ++frame) simulation.Step(.1f, {{agent.id, prepare}});
    Require(count(SoundKind::DecoyPrepare) == 1 && world.sounds[0].sequence == sequence && count(SoundKind::DecoyThrow) == 0 && world.noises.empty(),
      "Mantener la guía repite la preparación cada tick o reproduce un lanzamiento inexistente");
    simulation.Step(.05f, {});
    Require(!agent.decoyGuide.active && world.sounds.size() == 1, "Cancelar la guía reproduce otra preparación o deja activa una acción cancelada");
    simulation.Step(.05f, {{agent.id, prepare}});
    Require(count(SoundKind::DecoyPrepare) == 2 && world.sounds.back().sequence > sequence, "Una nueva preparación tras cancelar carece de su propia transición");
    ActionCommand launch; launch.throwDecoy = true; launch.aimX = 1.f; launch.decoyDistance = 160.f;
    simulation.Step(.05f, {{agent.id, launch}});
    Require(count(SoundKind::DecoyThrow) == 1 && count(SoundKind::Decoy) == 0 && world.decoys.size() == 1 && agent.loadout.decoys == 2 && world.noises.empty(),
      "El lanzamiento aceptado no tiene Foley propio, anticipa el impacto o duplica el ruido acústico");
    Require(std::ranges::all_of(world.sounds, [&agent](const SoundEvent &sound) { return sound.actor == agent.id && sound.position == agent.position && sound.elevation == 96.f; }),
      "Preparación y lanzamiento pierden la altura o el origen real del agente");
    std::string error;
    Require(replica.LoadSnapshot(simulation.SnapshotFor(agent.id, true), error) && replica.GetState().sounds.size() == 3 &&
      std::ranges::count(replica.GetState().sounds, SoundKind::DecoyPrepare, &SoundEvent::kind) == 2 &&
      std::ranges::count(replica.GetState().sounds, SoundKind::DecoyThrow, &SoundEvent::kind) == 1 && replica.GetState().decoys[0].elevation == 96.f,
      "La réplica pierde las identidades nuevas de preparación/lanzamiento o su planta");
    simulation.Step(.05f, {{agent.id, launch}});
    Require(count(SoundKind::DecoyThrow) == 1 && world.decoys.size() == 1 && agent.loadout.decoys == 2 && world.noises.empty(),
      "Un lanzamiento rechazado durante el enfriamiento produce otra voz o consume otro señuelo");
    for (int frame = 0; frame < 5; ++frame) simulation.Step(.1f, {});
    Require(world.decoys.empty() && count(SoundKind::Decoy) == 1 && world.noises.size() == 1 && world.noises[0].kind == SoundKind::Decoy && world.noises[0].elevation == 96.f,
      "El Foley de preparación/lanzamiento altera el único impacto original o su ruido de IA");
    for (const bool empty : {true, false}) {
      Simulation rejected; auto &invalid = AcousticFloors(rejected); auto &actor = invalid.agents[0];
      actor.loadout.decoys = empty ? 0 : 3;
      if (!empty) actor.actionCooldown = 1.f;
      rejected.Step(.05f, {{actor.id, prepare}}); rejected.Step(.05f, {{actor.id, launch}});
      Require(!actor.decoyGuide.active && invalid.decoys.empty() && invalid.sounds.empty() && invalid.noises.empty() && actor.loadout.decoys == (empty ? 0 : 3),
        "Preparar o lanzar sin inventario o durante el enfriamiento reproduce una acción rechazada");
    }
  }

  void CameraToolUsesItsOwnAcceptedMechanicalContact() {
    using namespace NoTrace;
    for (unsigned int variant = 0; variant < 5; ++variant) {
      Simulation simulation, replica; auto &world = AcousticFloors(simulation); auto &agent = world.agents[0]; agent.loadout.tools = variant == 1 ? 0 : 2;
      Interactable camera{41, ObjectKind::Camera, {variant == 4 ? 500.f : 330.f, 300.f}, "Cámara para intervención"};
      camera.footprint = {12.f, 12.f}; camera.solid = SpatialOverride::Disabled; camera.elevation = 96.f; camera.supportId = 2;
      camera.facing = {1.f, 0.f}; camera.camera.sweeping = false; camera.active = variant != 2;
      world.objects.push_back(camera);
      if (variant == 3) {
        Obstacle wall{{{314.f, 260.f}, {4.f, 80.f}}, CoverKind::High, true, "Muro entre herramienta y cámara"}; wall.elevation = 96.f; wall.height = 96.f;
        world.obstacles.push_back(wall);
      }
      ActionCommand use; use.tool = true; simulation.Step(.05f, {{agent.id, use}});
      const auto disabled = std::ranges::count(world.sounds, SoundKind::CameraDisable, &SoundEvent::kind);
      Require(std::ranges::none_of(world.sounds, [](const SoundEvent &sound) { return sound.kind == SoundKind::Camera; }),
        "Averiar o rechazar una cámara reproduce el servo de funcionamiento");
      if (variant != 0) {
        Require(disabled == 0 && world.noises.empty() && agent.loadout.tools == (variant == 1 ? 0 : 2) &&
          std::ranges::count(world.events, EventKind::CameraDisabled, &MissionEvent::kind) == 0,
          "Una herramienta agotada, cámara inactiva, oclusión o distancia inválida reproduce una avería aceptada");
        continue;
      }
      Require(disabled == 1 && !world.objects[0].active && world.objects[0].camera.state == CameraState::Broken && agent.loadout.tools == 1 &&
        world.sounds.back().position == camera.position && world.sounds.back().elevation == 96.f && world.noises.empty(),
        "La avería real no tiene un único contacto mecánico propio desde la cámara y su planta");
      std::string error;
      Require(replica.LoadSnapshot(simulation.SnapshotFor(agent.id, true), error) && replica.GetState().sounds.size() == 1 &&
        replica.GetState().sounds[0].kind == SoundKind::CameraDisable && replica.GetState().sounds[0].elevation == 96.f,
        "El cliente pierde el contacto de avería de cámara o su altura");
      simulation.Step(.05f, {{agent.id, use}});
      Require(std::ranges::count(world.sounds, SoundKind::CameraDisable, &SoundEvent::kind) == 1 && agent.loadout.tools == 1 &&
        std::ranges::count(world.events, EventKind::CameraDisabled, &MissionEvent::kind) == 1 && world.noises.empty(),
        "Repetir herramienta en cámara rota duplica recurso, incidente, inventario o ruido de IA");
    }
  }

  void FloorSlabsMuffleOnlyTheCrossingRay() {
    using namespace NoTrace;
    Simulation simulation; auto &world = AcousticFloors(simulation);
    auto listener = world.agents[0]; listener.id = 2; listener.elevation = 0.f; listener.supportId = 1;
    SoundEvent sound{1, SoundKind::Knock, {300.f, 300.f}, 150.f, 1.f, 0, 96.f};
    Require(world.obstacles.empty() && world.objects.empty() && AudioGeometry::Occluded(world, listener, sound.position, sound.elevation) &&
      !Visibility::SoundAudible(world, listener, sound), "Una losa sin paredes no atenúa el sonido procedente de otra planta");
    world.walkSurfaces.pop_back();
    Require(!AudioGeometry::Occluded(world, listener, sound.position, sound.elevation) && Visibility::SoundAudible(world, listener, sound),
      "Quitar únicamente la losa no restablece el rayo libre y el alcance sin muffling");
    world.walkSurfaces.push_back({2, 1, WalkSurfaceKind::Floor, {{100.f, 100.f}, {600.f, 600.f}}, 96.f});
    listener.elevation = 96.f; listener.supportId = 2; sound.position.x = 350.f;
    Require(!AudioGeometry::Occluded(world, listener, sound.position, sound.elevation) && Visibility::SoundAudible(world, listener, sound),
      "La losa de apoyo ocluye falsamente sonidos horizontales en la misma planta");
    FootstepCue cue{{300.f, 300.f}, 0, .5f, 81, 320.f};
    Require(!Visibility::FootstepAudible(world, listener, cue), "Una pista de pasos ignora su distancia vertical al oyente");
  }

  void CameraPanUsesOnlyTheLocalListeningFrame() {
    using namespace NoTrace;
    Require(AudioGeometry::Pan({50.f, 0.f}, 100.f) == .5f && AudioGeometry::Pan({0.f, 50.f}, 100.f) == 0.f,
      "El paneo táctico por defecto cambió sus coordenadas horizontales");
    Require(AudioGeometry::Pan({0.f, 50.f}, 100.f, sf::Vector2f{0.f, 2.f}) == .5f &&
      AudioGeometry::Pan({0.f, 50.f}, 100.f, sf::Vector2f{0.f, -1.f}) == -.5f &&
      AudioGeometry::Pan({50.f, 0.f}, 100.f, sf::Vector2f{0.f, 1.f}) == 0.f,
      "La escucha en primera persona no gira con el eje derecho normalizado de su cámara local");
    Require(AudioGeometry::Pan({500.f, 0.f}, 100.f, sf::Vector2f{1.f, 0.f}) == 1.f &&
      AudioGeometry::Pan({50.f, 0.f}, 100.f, sf::Vector2f{}) == .5f &&
      AudioGeometry::Pan({50.f, 0.f}, 100.f, sf::Vector2f{std::numeric_limits<float>::quiet_NaN(), 0.f}) == .5f,
      "Un eje local inválido o una fuente lejana deja paneo no finito o fuera de límites");
  }

  void ShotImpactKeepsTheContactHeight() {
    using namespace NoTrace;
    Simulation simulation; auto &world = AcousticFloors(simulation);
    Obstacle wall{{{400.f, 250.f}, {10.f, 100.f}}, CoverKind::High, true, "Muro elevado de impacto"}; wall.elevation = 96.f;
    world.obstacles.push_back(wall);
    ActionCommand fire; fire.fireLethal = true; fire.aimX = 1.f; simulation.Step(.1f, {{1, fire}});
    const auto impact = std::ranges::find(world.sounds, SoundKind::Impact, &SoundEvent::kind);
    Require(world.shots.size() == 1 && world.shots[0].blocked && impact != world.sounds.end() &&
      impact->elevation == world.shots[0].toHeight && impact->elevation > world.agents[0].elevation,
      "El impacto acústico usa la planta del tirador en vez de la altura real de contacto");
  }

  void SurfaceImpactFoleyTracksTheFirstGeometry() {
    using namespace NoTrace;
    for (unsigned int variant = 0; variant < 9; ++variant) {
      Simulation simulation; simulation.ResetBase(); simulation.Deploy(4);
      auto &world = const_cast<WorldState &>(simulation.GetState());
      world.guards.clear(); world.objects.clear(); world.obstacles.clear(); world.securityEntries.clear(); world.sounds.clear(); world.noises.clear();
      TestFixture::LegacyPlane(world);
      world.walkSurfaces.push_back({1, 0, WalkSurfaceKind::Floor, {{0.f, 0.f}, world.size}, 0.f});
      auto &agent = world.agents[0]; agent.position = {300.f, 300.f}; agent.facing = {1.f, 0.f}; agent.supportId = 1;
      SoundKind expected = SoundKind::ImpactMetal;
      if (variant <= 1) {
        WalkSurface slab{2, 1, WalkSurfaceKind::Catwalk, {{400.f, 250.f}, {100.f, 100.f}}, 32.f};
        slab.thickness = 16.f; slab.material = variant == 0 ? SurfaceMaterial::Steel : SurfaceMaterial::Grate; world.walkSurfaces.push_back(slab);
      } else if (variant <= 5) {
        Interactable locker{41, ObjectKind::Armory, {440.f, 300.f}, "Taquilla metálica"}; locker.propStyle = PropStyle::Locker; locker.footprint = {40.f, 40.f}; world.objects.push_back(locker);
        if (variant == 3 || variant == 5) {
          Obstacle wall{{{380.f, 250.f}, {10.f, 100.f}}, CoverKind::High, true, "Muro anterior a la taquilla"};
          if (variant == 5) wall.elevation = 96.f;
          world.obstacles.push_back(wall);
          if (variant == 3) expected = SoundKind::Impact;
        } else if (variant == 4) {
          Guard target; target.id = 81; target.position = {360.f, 300.f}; target.facing = {1.f, 0.f}; target.supportId = 1; world.guards.push_back(target);
          expected = SoundKind::ImpactBody;
        }
      } else if (variant == 6) {
        world.obstacles.push_back({{{400.f, 250.f}, {10.f, 100.f}}, CoverKind::High, true, "Hormigón sin material metálico"}); expected = SoundKind::Impact;
      } else if (variant == 7) world.walkSurfaces[0].material = SurfaceMaterial::Steel;
      const auto ammunition = agent.loadout.lethalAmmo;
      ActionCommand fire; fire.fireLethal = true; fire.aimX = 1.f; if (variant == 7) fire.aimSlope = -.5f;
      simulation.Step(.1f, {{agent.id, fire}});
      const auto impacts = std::ranges::count_if(world.sounds, [](const SoundEvent &sound) { return sound.kind == SoundKind::Impact || sound.kind == SoundKind::ImpactMetal || sound.kind == SoundKind::ImpactBody; });
      Require(world.shots.size() == 1 && world.noises.size() == 1 && agent.loadout.lethalAmmo == ammunition - 1 &&
        world.noises[0].kind == SoundKind::Suppressed && std::ranges::count(world.events, EventKind::WeaponLethal, &MissionEvent::kind) == 1,
        "Elegir el material de impacto duplica el disparo, el ruido o el consumo autoritativo");
      if (variant == 8) {
        Require(!world.shots[0].blocked && !world.shots[0].hit && impacts == 0, "Un disparo al aire reproduce una superficie inexistente");
        continue;
      }
      const auto sound = std::ranges::find(world.sounds, expected, &SoundEvent::kind);
      Require(impacts == 1 && sound != world.sounds.end() && sound->position == world.shots[0].to && sound->elevation == world.shots[0].toHeight,
        "La primera geometría realmente alcanzada no produce un único contacto con su material y altura");
      Require(variant == 4 ? world.shots[0].hit && world.shots[0].targetKind == ShotTarget::Guard : world.shots[0].blocked && !world.shots[0].hit,
        "La selección sonora atraviesa la primera cobertura o cambia un impacto corporal por superficie");
      if (variant == 3) Require(sound->position.x == 380.f && std::ranges::none_of(world.sounds, [](const SoundEvent &event) { return event.kind == SoundKind::ImpactMetal; }),
        "El metal detrás de una pared ocluyente impone su sonido al impacto cercano de hormigón");
    }
  }

  void ObjectiveFeedbackRequiresTheFirstAcceptedInteraction() {
    using namespace NoTrace;
    for (const auto kind : {ObjectKind::Terminal, ObjectKind::Secondary}) for (unsigned int variant = 0; variant < 4; ++variant) {
      Simulation simulation; simulation.ResetBase(); simulation.Deploy(4);
      auto &world = const_cast<WorldState &>(simulation.GetState());
      world.guards.clear(); world.objects.clear(); world.obstacles.clear(); world.securityEntries.clear(); world.sounds.clear(); world.noises.clear();
      TestFixture::LegacyPlane(world); world.agents[0].position = {300.f, 300.f};
      Interactable object{41, kind, {variant == 3 ? 500.f : 330.f, 300.f}, "Archivo de prueba"}; object.solid = SpatialOverride::Disabled; object.footprint = {12.f, 12.f};
      if (variant == 2) object.interactive = false;
      world.objects.push_back(object);
      if (variant == 1) world.obstacles.push_back({{{314.f, 260.f}, {4.f, 80.f}}, CoverKind::High, true, "Muro entre mano y terminal"});
      ActionCommand use; use.interact = true; simulation.Step(.1f, {{1, use}});
      const auto feedback = [&world] { return std::ranges::count(world.sounds, SoundKind::ComputerKey, &SoundEvent::kind); };
      if (variant == 0) {
        Require(world.objects[0].completed && feedback() == 1 && (kind == ObjectKind::Terminal ? world.objectives == 1 : world.secondary == 1),
          "Recuperar realmente el archivo no emite una única respuesta del terminal");
        simulation.Step(.1f, {{1, use}});
        Require(feedback() == 1 && world.noises.empty() && std::ranges::count(world.events, kind == ObjectKind::Terminal ? EventKind::Objective : EventKind::Secondary, &MissionEvent::kind) == 1,
          "Repetir E en un archivo recuperado reproduce contacto o duplica el objetivo y ruido de IA");
      } else Require(!world.objects[0].completed && feedback() == 0 && world.objectives == 0 && world.secondary == 0 && world.noises.empty(),
        "Una interacción ocluida, no interactiva o fuera de alcance reproduce un terminal aceptado");
    }
  }

  void AcousticElevationIsReplicatedWithoutPrivateCoordinates() {
    using namespace NoTrace;
    Simulation simulation, replica; auto &world = AcousticFloors(simulation);
    auto &listener = world.agents[0]; listener.elevation = 0.f; listener.supportId = 1; listener.facing = {1.f, 0.f};
    world.walkSurfaces[1].elevation = 111.25f;
    Guard hidden; hidden.id = 91; hidden.position = {280.f, 300.f}; hidden.elevation = 111.25f; hidden.supportId = 2;
    world.guards.push_back(hidden);
    world.sounds = {{1, SoundKind::Knock, hidden.position, 1000.f, 1.f, 2, 111.25f}, {2, SoundKind::Knock, hidden.position, 1000.f, 1.f, 1, 111.25f}};
    world.noises = {{hidden.position, 1000.f, .6f, 2, 91, SoundKind::Knock, .6f, {}, 111.25f}, {hidden.position, 1000.f, .6f, 1, 0, SoundKind::Knock, .6f, {}, 111.25f}};
    world.footsteps = {{DistractionGeometry::QuantizeFootstep(hidden.position), 0, .5f, 91, 111.25f}};
    world.decoys = {{{300.f, 300.f}, {400.f, 300.f}, .1f, 1.f, DistractionConfig::DecoyArcHeight, 1, false, 111.25f}};
    const auto filtered = simulation.SnapshotFor(1, true);
    Require(filtered["sounds"].size() == 2 && filtered["noises"].size() == 2 && filtered["footsteps"].size() == 1 && filtered["decoys"].size() == 1,
      "El fixture no conserva los eventos acústicos autorizados y el disco propio");
    Require(filtered["sounds"][0]["elevation"] == 96.f && filtered["sounds"][0]["actor"] == 0 &&
      filtered["noises"][0]["elevation"] == 96.f && filtered["noises"][0]["guardId"] == 0 &&
      filtered["footsteps"][0]["elevation"] == 96.f && !filtered["footsteps"][0].contains("sourceGuard"),
      "La señal oculta revela la cota exacta o metadatos privados de su emisor");
    Require(filtered["sounds"][1]["elevation"] == 111.25f && filtered["noises"][1]["elevation"] == 111.25f && filtered["decoys"][0]["elevation"] == 111.25f,
      "La cuantización modifica los eventos o el disco propios");
    std::string error;
    Require(replica.LoadSnapshot(filtered, error) && replica.GetState().sounds[0].elevation == 96.f && replica.GetState().decoys[0].elevation == 111.25f,
      "La réplica pierde la altura acústica o la base del disco en vuelo");
    for (const auto collection : {"sounds", "noises", "footsteps", "decoys"}) {
      for (const float invalidHeight : {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(), 100000.f, -100000.f}) {
        auto invalid = filtered; invalid[collection][0]["elevation"] = invalidHeight;
        Require(!replica.LoadSnapshot(invalid, error) && replica.GetState().sounds[0].elevation == 96.f, "El codec acepta una altura inválida o destruye la réplica anterior");
      }
      auto missing = filtered; missing[collection][0].erase("elevation");
      Require(!replica.LoadSnapshot(missing, error), "El codec acepta un evento acústico sin altura explícita");
    }
  }

  void InaccessibleFloorReleasesAssistanceWithoutWaking() {
    using namespace NoTrace;
    Simulation simulation; simulation.ResetBase(); simulation.Deploy(0);
    auto &world = const_cast<WorldState &>(simulation.GetState());
    world.objects.clear(); world.obstacles.clear(); world.guards.clear(); world.securityEntries.clear(); world.sounds.clear(); world.noises.clear();
    TestFixture::LegacyPlane(world);
    world.walkSurfaces = {{1, 0, WalkSurfaceKind::Floor, {{0.f, 0.f}, world.size}, 0.f}, {2, 1, WalkSurfaceKind::Floor, {{200.f, 200.f}, {400.f, 400.f}}, 96.f}};
    world.agents[0].position = {50.f, 700.f}; world.agents[0].hidden = true; world.agents[0].supportId = 1;
    Guard helper; helper.id = 81; helper.position = {300.f, 300.f}; helper.supportId = 1; helper.state = GuardState::ApproachBody;
    helper.bodyTargetKind = BodyTargetKind::Guard; helper.bodyTargetId = 91;
    Guard body; body.id = 91; body.position = {310.f, 300.f}; body.elevation = 96.f; body.supportId = 2;
    body.state = GuardState::Incapacitated; body.health = 37.f; body.stun = CombatConfig::MaxGuardStun; body.incapacityRemaining = 80.f;
    world.guards = {helper, body}; simulation.Step(.1f, {});
    Require(world.guards[0].bodyTargetKind == BodyTargetKind::None && world.guards[0].navigationRejectTime > 0.f && world.guards[0].state == GuardState::Patrol,
      "Una planta sin conexión mantiene una reserva de asistencia e impide continuar la IA");
    for (int frame = 0; frame < 20; ++frame) simulation.Step(.1f, {});
    Require(world.guards[0].elevation == 0.f && world.guards[1].elevation == 96.f && world.guards[1].state == GuardState::Incapacitated && world.guards[1].health == 37.f &&
      std::ranges::none_of(world.sounds, [](const SoundEvent &sound) { return sound.kind == SoundKind::Wake; }), "Una asistencia a través de otra planta despierta el cuerpo o reproduce un despertar falso");
  }

  void PersonalLifeSoundsDoNotRevealAnotherActor() {
    using namespace NoTrace;
    WorldState world; Agent listener; listener.id = 1; listener.alive = false; listener.lifeState = LifeState::Dead;
    SoundEvent sound{1, SoundKind::Death, listener.position, 70.f, 1.f, 1};
    Require(Visibility::SoundAudible(world, listener, sound), "La víctima pierde el sonido de su propia muerte al actualizar su estado vital");
    sound.actor = 2;
    Require(!Visibility::SoundAudible(world, listener, sound), "La excepción de muerte permite escuchar otro emisor tras quedar fuera de combate");
    sound.actor = 1; sound.kind = SoundKind::Pistol;
    Require(!Visibility::SoundAudible(world, listener, sound), "La excepción vital habilita cualquier sonido propio estando muerto");
    sound.kind = SoundKind::Hurt; sound.remaining = 0.f;
    Require(!Visibility::SoundAudible(world, listener, sound), "La excepción vital revive un evento de audio caducado");
  }

  void FootstepVisualDirectionUsesTheEmittedMovement() {
    using namespace NoTrace;
    const auto prepare = [](Simulation &simulation) -> WorldState & {
      simulation.ResetBase(); simulation.Deploy(0);
      auto &world = const_cast<WorldState &>(simulation.GetState());
      world.obstacles.clear(); world.objects.clear(); world.guards.clear(); world.sounds.clear(); world.noises.clear(); world.securityEntries.clear();
      TestFixture::LegacyPlane(world);
      world.agents[0].position = {400.f, 400.f}; world.agents[0].facing = {1.f, 0.f};
      return world;
    };
    Simulation simulation, replica;
    auto &world = prepare(simulation);
    world.agents[0].footstepTime = GameplayConfig::RunningFootstepInterval;
    ActionCommand east; east.moveX = 1.f; east.run = true; simulation.Step(0.05f, {{1, east}});
    Require(world.noises.size() == 1 && world.noises[0].visualDirection == sf::Vector2f{1.f, 0.f} && world.noises[0].position == world.agents[0].position && world.noises[0].radius == GameplayConfig::RunningNoiseRadius,
      "El paso no captura su dirección real manteniendo el origen y radio físicos");
    const auto emitted = world.noises[0];
    ActionCommand north; north.moveY = -1.f; simulation.Step(0.05f, {{1, north}}); simulation.Step(0.05f, {});
    Require(world.noises.size() == 1 && !world.agents[0].moving && world.agents[0].facing == sf::Vector2f{0.f, -1.f} && world.noises[0].visualDirection == emitted.visualDirection && world.noises[0].position == emitted.position && world.noises[0].radius == emitted.radius,
      "Girar y detenerse reorienta o desplaza una onda que ya fue emitida");
    std::string error;
    const auto snapshot = simulation.SnapshotFor(1, true);
    Require(replica.LoadSnapshot(snapshot, error) && replica.GetState().noises[0].visualDirection == emitted.visualDirection && replica.GetState().noises[0].position == emitted.position,
      "La réplica no conserva la dirección inmutable del paso propio");
    const std::array<nlohmann::json, 7> invalidDirections{{nullptr, nlohmann::json::array({1.f}), nlohmann::json::array({1.f, 0.f, 0.f}),
      nlohmann::json::array({"este", 0.f}), nlohmann::json::array({2.f, 0.f}), nlohmann::json::array({0.5f, 0.f}),
      nlohmann::json::array({std::numeric_limits<float>::quiet_NaN(), 0.f})}};
    for (const auto &direction : invalidDirections) {
      auto invalid = snapshot; invalid["noises"][0]["visualDirection"] = direction;
      Require(!replica.LoadSnapshot(invalid, error) && replica.GetState().noises[0].visualDirection == emitted.visualDirection,
        "Una dirección visual malformada altera una réplica válida");
    }
    auto invalid = snapshot; invalid["noises"][0].erase("visualDirection");
    Require(!replica.LoadSnapshot(invalid, error), "El protocolo nuevo acepta un ruido sin metadatos explícitos");
    invalid = snapshot; invalid["version"] = 9;
    Require(!replica.LoadSnapshot(invalid, error), "El protocolo anterior acepta un esquema de ruido incompatible");
    invalid = snapshot; invalid["noises"][0]["kind"] = static_cast<int>(SoundKind::Knock);
    Require(!replica.LoadSnapshot(invalid, error), "Un golpe admite una dirección visual reservada a pasos");

    auto &blocked = prepare(simulation);
    blocked.obstacles.push_back({{{412.f, 250.f}, {24.f, 300.f}}, CoverKind::High, true, "Deslizamiento físico"});
    blocked.agents[0].footstepTime = GameplayConfig::FootstepInterval;
    ActionCommand diagonal; diagonal.moveX = 1.f; diagonal.moveY = 1.f;
    simulation.Step(0.05f, {{1, diagonal}});
    Require(blocked.noises.size() == 1 && blocked.agents[0].position.x == 400.f && blocked.agents[0].position.y > 400.f && blocked.agents[0].facing.x > 0.f && blocked.noises[0].visualDirection == sf::Vector2f{0.f, 1.f},
      "La onda usa la orden diagonal o facing en lugar del desplazamiento que permite la colisión");

    auto &cover = prepare(simulation); cover.agents[0].position = {387.f, 400.f};
    cover.obstacles.push_back({{{412.f, 200.f}, {24.f, 400.f}}, CoverKind::High, true, "Pared de cobertura"});
    ActionCommand enter; enter.coverToggle = true; simulation.Step(0.05f, {{1, enter}});
    cover.agents[0].footstepTime = GameplayConfig::FootstepInterval;
    ActionCommand slide; slide.moveY = -1.f; simulation.Step(0.05f, {{1, slide}});
    Require(cover.agents[0].cover.active && cover.agents[0].facing == sf::Vector2f{-1.f, 0.f} && cover.noises.size() == 1 && cover.noises[0].visualDirection == sf::Vector2f{0.f, -1.f},
      "El paso lateral en cobertura se orienta por la normal corporal en lugar de su movimiento tangencial");
    Require(replica.LoadSnapshot(simulation.SnapshotFor(1), error) && replica.GetState().noises[0].visualDirection == sf::Vector2f{0.f, -1.f},
      "La serialización completa pierde el paso tangencial de cobertura");

    auto &privateWorld = prepare(simulation); privateWorld.agents[0].position = {300.f, 300.f};
    privateWorld.noises = {{{240.f, 300.f}, 200.f, GameplayConfig::NoiseDuration, 2, 0, SoundKind::StepRunHard, GameplayConfig::NoiseDuration, {0.f, 1.f}},
      {{360.f, 300.f}, 200.f, GameplayConfig::NoiseDuration, 2, 0, SoundKind::StepHard, GameplayConfig::NoiseDuration, {-1.f, 0.f}}};
    Require(!Visibility::IsVisible(privateWorld, privateWorld.agents[0], privateWorld.noises[0].position) && Visibility::IsVisible(privateWorld, privateWorld.agents[0], privateWorld.noises[1].position),
      "La fixture privada debe separar un origen oculto de otro visible");
    const auto filtered = simulation.SnapshotFor(1, true);
    Require(filtered["noises"].size() == 2 && filtered["noises"][0]["position"] != nlohmann::json::array({240.f, 300.f}) &&
      filtered["noises"][0]["visualDirection"] == nlohmann::json::array({0.f, 0.f}) && filtered["noises"][1]["visualDirection"] == nlohmann::json::array({0.f, 0.f}),
      "El ruido ajeno o cuantizado revela una dirección reservada al movimiento propio");
    Require(replica.LoadSnapshot(filtered, error) && replica.GetState().noises[0].visualDirection == sf::Vector2f{}, "La réplica rechaza una onda ajena anonimizada");
    invalid = filtered; invalid["noises"][0]["visualDirection"] = {0.f, 1.f};
    Require(!replica.LoadSnapshot(invalid, error) && replica.GetState().noises[0].visualDirection == sf::Vector2f{}, "La réplica acepta dirección privada en un ruido de emisor oculto");
  }

  void GuardHearingUsesEventRadiusAcrossWalls() {
    using namespace NoTrace;
    struct AcousticCase { SoundKind kind; float radius; };
    const std::array cases{
      AcousticCase{SoundKind::StepHard, GameplayConfig::WalkingNoiseRadius},
      AcousticCase{SoundKind::StepRunHard, GameplayConfig::RunningNoiseRadius},
      AcousticCase{SoundKind::Pistol, GameplayConfig::LethalNoiseRadius},
      AcousticCase{SoundKind::Suppressed, GameplayConfig::NonLethalNoiseRadius},
      AcousticCase{SoundKind::Knock, DistractionConfig::KnockHearingDistance},
      AcousticCase{SoundKind::Decoy, DistractionConfig::DecoyHearingDistance},
      AcousticCase{SoundKind::Alarm, AudioConfig::AlarmRadius},
      AcousticCase{SoundKind::Explosion, DevicesConfig::ExplosionNoiseRadius}};
    for (const auto &acoustic : cases) for (const int walls : {0, 1, 2}) for (const float edge : {-1.f, 0.f, 1.f}) {
      Simulation simulation; simulation.ResetBase(); simulation.Deploy(0);
      auto &world = const_cast<WorldState &>(simulation.GetState());
      world.size = {2000.f, 1600.f}; world.objects.clear(); world.obstacles.clear(); world.guards.clear();
      world.noises.clear(); world.sounds.clear(); world.events.clear(); world.securityEntries.clear();
      TestFixture::LegacyPlane(world);
      world.agents[0].position = {50.f, 1400.f}; world.agents[0].hidden = true;
      const sf::Vector2f origin{300.f, 500.f};
      Guard guard; guard.id = 81; guard.position = origin + sf::Vector2f{acoustic.radius + edge, 0.f}; guard.facing = {1.f, 0.f};
      world.guards.push_back(guard);
      for (int wall = 0; wall < walls; ++wall) world.obstacles.push_back({{{320.f + wall * 20.f, 450.f}, {2.f, 100.f}}, CoverKind::High, true, "Separación acústica"});
      if (walls > 0) Require(!Visibility::HasLineOfSight(world, guard.position, origin), "El caso acústico debe tener una pared real entre emisor y guardia");
      world.noises.push_back({origin, acoustic.radius, 0.6f, 1, 0, acoustic.kind});
      simulation.Step(0.05f, {});
      const auto &heard = world.guards[0];
      Require(edge <= 0.f ? heard.state == GuardState::Investigate && heard.lastKnown == origin : heard.state == GuardState::Patrol,
        "La audición enemiga debe respetar el borde del radio específico aun detrás de varias paredes");
      Require(!world.alarm && world.securityAlert == SecurityAlert::Calm && heard.observedAgent == 0 && !world.agents[0].detectionConfirmed,
        "Escuchar un evento no confirma visión, identidad ni ubicación actual del jugador");
      if (edge <= 0.f) {
        world.agents[0].position += sf::Vector2f{100.f, 0.f}; simulation.Step(0.05f, {});
        Require(world.guards[0].lastKnown == origin, "La pista acústica debe permanecer en el evento y no seguir al emisor");
      }
    }
  }
}
int main() {
  try {
    using namespace NoTrace;
    FootstepVisualDirectionUsesTheEmittedMovement();
    GuardHearingUsesEventRadiusAcrossWalls();
    Require(std::abs(GameplayConfig::RunningNoiseRadius - 306.f * 0.8f) < .001f && std::abs(GameplayConfig::RunningNoiseRadius - 244.8f) < .001f,
      "La carrera debe reducir exactamente un veinte por ciento el valor configurado de 306 unidades");
    Require(GameplayConfig::WalkingNoiseRadius == 70.f && GameplayConfig::CrouchedNoiseRadius == 12.f && GameplayConfig::CrouchedRunningNoiseRadius == 22.f,
      "Reducir la carrera no debe cambiar los radios de caminar o moverse agachado");
    const auto root = std::filesystem::exists("Content") ? std::filesystem::path(".") : std::filesystem::path("..");
    RecordedLibraryHasIndependentVariants(root);
    MixerUsesBusesPrioritiesAndStableLoops(root);
    MissingResourceCanBeRetried(root);
    ContactFoleyRequiresAPhysicalResult();
    EmptyTriggerHasFeedbackWithoutAProjectileOrSpam();
    FootstepHearingUsesThePhysicalFloor();
    DecoyFoleyTracksPreparationAndAcceptedThrows();
    CameraToolUsesItsOwnAcceptedMechanicalContact();
    FloorSlabsMuffleOnlyTheCrossingRay();
    CameraPanUsesOnlyTheLocalListeningFrame();
    ShotImpactKeepsTheContactHeight();
    SurfaceImpactFoleyTracksTheFirstGeometry();
    ObjectiveFeedbackRequiresTheFirstAcceptedInteraction();
    AcousticElevationIsReplicatedWithoutPrivateCoordinates();
    InaccessibleFloorReleasesAssistanceWithoutWaking();
    PersonalLifeSoundsDoNotRevealAnotherActor();
    Simulation simulation, replica; simulation.ResetBase(); simulation.Deploy(7);
    auto &world = const_cast<WorldState &>(simulation.GetState());
    world.obstacles.clear(); world.objects.clear(); world.guards.clear(); world.sounds.clear(); world.noises.clear(); world.decoys.clear(); world.securityEntries.clear();
    TestFixture::LegacyPlane(world);
    auto &agent = world.agents[0]; agent.position = {300.f,300.f}; agent.facing = {1.f,0.f};
    world.noises.push_back({{230.f,300.f},200.f,1.f,1,0,SoundKind::Knock,1.f});
    world.noises.push_back({{240.f,300.f},200.f,1.f,0,99,SoundKind::Decoy,1.f});
    world.noises.push_back({{900.f,700.f},40.f,1.f,0,98,SoundKind::Rifle,1.f});
    Require(!Visibility::IsVisible(world,agent,{230.f,300.f}) && Visibility::NoiseAudible(world,agent,world.noises[0]), "La niebla oculta el ruido propio");
    world.sounds.push_back({1,SoundKind::TrapTick,{350.f,300.f},180.f,1.f,0});
    world.sounds.push_back({2,SoundKind::Rifle,{900.f,700.f},50.f,1.f,0});
    const auto snapshot = simulation.SnapshotFor(1,true);
    Require(snapshot["noises"].size()==2 && snapshot["noises"][1]["guardId"]==0, "El ruido no se filtra por audición o revela al enemigo");
    Require(snapshot["noises"][0]["kind"]==static_cast<int>(SoundKind::Knock) && snapshot["noises"][1]["kind"]==static_cast<int>(SoundKind::Decoy) && snapshot["noises"][0]["duration"]==1.f, "El evento acústico pierde tipo y duración al replicarse");
    Require(snapshot["noises"][0]["position"]==nlohmann::json::array({230.f,300.f}) && snapshot["noises"][1]["position"]!=nlohmann::json::array({240.f,300.f}), "La señal propia se desplaza o la ajena revela una ubicación oculta exacta");
    Require(snapshot["sounds"].size()==1, "El audio revela una acción fuera de audición");
    std::string error; Require(replica.LoadSnapshot(snapshot,error), "El cliente rechaza señales acústicas válidas");
    auto invalid=snapshot; invalid["sounds"][0]["kind"]=99;
    Require(!replica.LoadSnapshot(invalid,error), "El cliente acepta un sonido inválido");
    invalid=snapshot; invalid["sounds"][0]["remaining"]=-1;
    Require(!replica.LoadSnapshot(invalid,error), "El cliente acepta una duración inválida");
    Require(replica.GetState().sounds.size()==1, "El fallo de carga destruye la réplica anterior");
    auto expanded = snapshot;
    for (std::size_t index = 0; index < AudioConfig::Profiles.size(); ++index) {
      expanded["sounds"][0]["kind"] = index;
      Require(replica.LoadSnapshot(expanded, error) && replica.GetState().sounds[0].kind == static_cast<SoundKind>(index), "Una identidad sonora nueva se pierde al replicarse");
    }
    expanded["sounds"][0]["kind"] = static_cast<int>(SoundKind::Count);
    Require(!replica.LoadSnapshot(expanded, error), "El sentinel del catálogo se acepta como sonido reproducible");
    invalid=snapshot; invalid["noises"][0]["kind"]=99;
    Require(!replica.LoadSnapshot(invalid,error), "El cliente acepta un tipo de ruido que no corresponde a ninguna acción");
    invalid=snapshot; invalid["noises"][0]["duration"]=0.5f;
    Require(!replica.LoadSnapshot(invalid,error), "El cliente acepta un ruido cuya duración total es menor que el tiempo restante");
    Require(AudioConfig::Gain(SoundKind::StepRunHard) > AudioConfig::Gain(SoundKind::StepHard) && AudioConfig::Gain(SoundKind::StepHard) > AudioConfig::Gain(SoundKind::StepCrouchHard), "Los pasos no distinguen la intensidad de correr, caminar y agacharse");
    Require(AudioConfig::File(SoundKind::SuppressorAttach) != AudioConfig::File(SoundKind::SuppressorDetach), "Montar y desmontar el silenciador reutilizan la misma señal mecánica");
    struct StepCase { Stance stance; bool run; float radius; SoundKind kind; };
    const std::array<StepCase,5> steps{{{Stance::Standing,false,GameplayConfig::WalkingNoiseRadius,SoundKind::StepHard}, {Stance::Standing,true,GameplayConfig::RunningNoiseRadius,SoundKind::StepRunHard},
      {Stance::Crouched,false,GameplayConfig::CrouchedNoiseRadius,SoundKind::StepCrouchHard}, {Stance::Crouched,true,GameplayConfig::CrouchedRunningNoiseRadius,SoundKind::StepCrouchHard}, {Stance::Prone,false,GameplayConfig::ProneNoiseRadius,SoundKind::StepHard}}};
    for (const auto &step : steps) {
      Simulation moving; moving.ResetBase(); moving.Deploy(4); auto &state=const_cast<WorldState&>(moving.GetState());
      state.obstacles.clear(); state.objects.clear(); state.guards.clear(); state.sounds.clear(); state.noises.clear(); state.decoys.clear(); state.securityEntries.clear();
      TestFixture::LegacyPlane(state);
      state.agents[0].position={100.f,500.f}; state.agents[0].stance=step.stance;
      ActionCommand command; command.moveX=1.f; command.run=step.run;
      for(int frame=0;frame<7;++frame) moving.Step(0.1f,{{1,command}});
      if (step.stance == Stance::Prone) {
        Require(state.noises.empty() && !state.sounds.empty() && state.agents[0].moving && std::ranges::all_of(state.sounds, [](const SoundEvent &sound) { return sound.kind == SoundKind::ProneCrawl; }), "Arrastrarse tumbado produce ruido de IA, silencio o pasos en lugar de ropa discreta");
        command.moveX = 0.f; command.fireLethal = true; command.aimX = 1.f;
        moving.Step(0.1f, {{1, command}});
        Require(!state.noises.empty() && !state.sounds.empty() && state.noises.back().visualDirection == sf::Vector2f{}, "La postura tumbada suprime el disparo independiente o le añade desplazamiento visual de pasos");
        continue;
      }
      Require(!state.noises.empty() && state.noises.back().radius==step.radius && step.radius>0.f, "Una postura o carrera elimina el ruido real o usa el mismo alcance para cualquier movimiento");
      Require(state.noises.back().actor==1 && state.noises.back().kind==step.kind && state.noises.back().duration==GameplayConfig::NoiseDuration, "Los pasos no conservan emisor, postura y duración de la simulación");
      Require(state.noises.back().visualDirection == sf::Vector2f{1.f, 0.f}, "Un paso de pie, agachado o corriendo pierde su dirección capturada");
      Require(state.noises.size() <= 3 && state.sounds.size() <= 3, "El movimiento emite una señal acústica cada fotograma");
      moving.Step(0.1f, {});
      Require(!state.agents[0].moving && state.agents[0].footstepTime == 0.f, "Detenerse conserva un temporizador de pasos pendiente");
    }
    std::cout << "PASS audio y percepción acústica: " << checks << " comprobaciones\n";
  } catch (const std::exception &e) { std::cerr << "FAIL audio: " << e.what() << '\n'; return 1; }
}
