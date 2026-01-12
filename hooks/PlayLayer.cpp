#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/ui/Notification.hpp>
#include <geode.custom-keybinds/include/OptionalAPI.hpp>
#include <Geode/binding/PlayerObject.hpp>
#include <Geode/binding/HardStreak.hpp>
#include "../layers/CapturePreviewPopup.hpp"
#include "../utils/FramebufferCapture.hpp"
#include "../utils/RenderTexture.hpp"
#include "../utils/Localization.hpp"
#include "../managers/LocalThumbs.hpp"
#include "../managers/ThumbnailAPI.hpp"
#include "../managers/PendingQueue.hpp"
#include "../utils/ImageConverter.hpp"
#include <Geode/binding/GameManager.hpp>
#include <Geode/binding/UILayer.hpp>
#include "../utils/DominantColors.hpp"
#include "../managers/LevelColors.hpp"
#include <cstring>
#include <memory>

using namespace geode::prelude;
using namespace keybinds;

namespace {
    std::atomic_bool gCaptureInProgress{false};

    CCNode* findGameplayNode(CCNode* root) {
        if (!root) return nullptr;
        auto* children = root->getChildren();
        if (!children) return nullptr;
        CCObject* obj = nullptr;
        
        // 1. Búsqueda prioritaria: GJBaseGameLayer or GameLayer directo
        CCARRAY_FOREACH(children, obj) {
            if (auto* node = typeinfo_cast<CCNode*>(obj)) {
                if (typeinfo_cast<GJBaseGameLayer*>(node)) {
                    log::info("[FindGameplay] Found GJBaseGameLayer");
                    return node;
                }
            }
        }

        // 2. Búsqueda by ID específico "game-layer" (común in mods/geode)
        CCARRAY_FOREACH(children, obj) {
            if (auto* node = typeinfo_cast<CCNode*>(obj)) {
                std::string id = node->getID();
                if (id == "game-layer" || id == "GameLayer") {
                    log::info("[FindGameplay] Found by ID: {}", id);
                    return node;
                }
            }
        }

        // 3. Búsqueda recursiva (pero evitando UILayer and PauseLayer)
        CCARRAY_FOREACH(children, obj) {
            if (auto* node = dynamic_cast<CCNode*>(obj)) {
                std::string cls = typeid(*node).name();
                std::string id = node->getID();
                
                // Skip UI containers
                if (cls.find("UILayer") != std::string::npos || id == "UILayer") continue;
                if (cls.find("PauseLayer") != std::string::npos) continue;

                if (auto* found = findGameplayNode(node)) return found;
            }
        }
        return nullptr;
    }

    bool buildPathToNode(CCNode* root, CCNode* target, std::vector<CCNode*>& path) {
        if (!root) return false;
        path.push_back(root);
        if (root == target) return true;
        auto* children = root->getChildren();
        if (children) {
            CCObject* obj = nullptr;
            CCARRAY_FOREACH(children, obj) {
                if (auto* node = dynamic_cast<CCNode*>(obj)) {
                    if (buildPathToNode(node, target, path)) return true;
                }
            }
        }
        path.pop_back();
        return false;
    }

    void hideSiblingsOutsidePath(const std::vector<CCNode*>& path, std::vector<CCNode*>& hidden) {
        if (path.size() < 2) return;
        for (size_t i = 0; i + 1 < path.size(); ++i) {
            auto* parent = path[i];
            auto* keepChild = path[i + 1];
            auto* children = parent->getChildren();
            if (!children) continue;
            CCObject* obj = nullptr;
            CCARRAY_FOREACH(children, obj) {
                if (auto* node = dynamic_cast<CCNode*>(obj)) {
                    if (node != keepChild && node->isVisible()) {
                        node->setVisible(false);
                        hidden.push_back(node);
                    }
                }
            }
        }
    }

    bool isNonGameplayOverlay(CCNode* node, bool checkZ) {
        if (!node) return false;
        
        // EXCEPTION: PlayerObject should never be hidden as UI
        if (typeinfo_cast<PlayerObject*>(node)) return false;

        // 1. Z-Order check (Solo para hijos directos de PlayLayer)
        if (checkZ && node->getZOrder() >= 10) return true;

        // 2. Heurísticas basadas in nombre of clase
        std::string cls = typeid(*node).name();
        auto clsL = cls; for (auto& c : clsL) c = (char)tolower(c);
        
        if (clsL.find("uilayer") != std::string::npos ||
            clsL.find("pause") != std::string::npos ||
            clsL.find("menu") != std::string::npos ||
            clsL.find("dialog") != std::string::npos ||
            clsL.find("popup") != std::string::npos ||
            clsL.find("editor") != std::string::npos ||
            clsL.find("notification") != std::string::npos ||
            (clsL.find("label") != std::string::npos && clsL.find("gameobject") == std::string::npos) || // Excluir LabelGameObject
            clsL.find("progress") != std::string::npos ||
            clsL.find("status") != std::string::npos || // MegaHack status
            clsL.find("trajectory") != std::string::npos || // ShowTrajectory
            clsL.find("hitbox") != std::string::npos) return true;

        // 3. Heurísticas basadas in ID
        std::string id = node->getID();
        auto idL = id; for (auto& c : idL) c = (char)tolower(c);
        if (!idL.empty()) {
            static const std::vector<std::string> patterns = {
                "ui", "uilayer", "pause", "menu", "dialog", "popup", "editor", "notification", "btn", "button", "overlay", "checkpoint", "fps", "debug", "attempt", "percent", "progress", "bar", "score", "practice", "hitbox", "trajectory", "status"
            };
            for (auto const& p : patterns) {
                if (idL.find(p) != std::string::npos) return true;
            }
        }
        
        // 4. Tipos explícitos
        if (dynamic_cast<CCMenu*>(node) != nullptr) return true;
        
        // Mejor estrategia para CCLabelBMFont:
        if (auto* label = dynamic_cast<CCLabelBMFont*>(node)) {
             // Si es hijo directo de PlayLayer y tiene Z >= 10, es casi seguro UI.
             // If tiene Z bajo, podría ser texto of the nivel.
             if (checkZ && node->getZOrder() >= 10) return true;
             // Si no es hijo directo, confiamos en el ID o nombre de clase (ya chequeado)
        }

        return false;
    }

    // Recorre recursivamente para ocultar UI
    // checkZ: true solo para el primer nivel (hijos directos de PlayLayer)
    void hideNonGameplayDescendants(CCNode* root, std::vector<CCNode*>& hidden, bool checkZ, PlayLayer* pl) {
        if (!root) return;
        auto* children = root->getChildren();
        if (!children) return;

        CCObject* obj = nullptr;
        CCARRAY_FOREACH(children, obj) {
            auto* node = dynamic_cast<CCNode*>(obj);
            if (!node) continue;

            // EXCEPTION: Explicitly check against player objects if PlayLayer is provided
            if (pl) {
                if (node == pl->m_player1 || node == pl->m_player2) continue;
            }

            // Si es un nodo de UI conocido, ocultarlo y NO descender
            if (node->isVisible() && isNonGameplayOverlay(node, checkZ)) {
                node->setVisible(false);
                hidden.push_back(node);
                log::info("[Capture] Hide: ID='{}', Class='{}', Z={}", node->getID(), typeid(*node).name(), node->getZOrder());
            } 
            // If Not es UI, descendemos only if parece a contenedor genérico
            // Desactivamos checkZ para niveles profundos para no ocultar objetos del juego
            else {
                std::string cls = typeid(*node).name();
                // Only descender if es a nodo contenedor genérico or Layer
                // NO descender en GameObjects, BatchNodes, ParticleSystems, etc.
                if (cls.find("CCNode") != std::string::npos || cls.find("Layer") != std::string::npos) {
                     // Excepción: Not descender in GJBaseGameLayer or GameLayer (the juego in sí)
                     if (cls.find("GameLayer") == std::string::npos) {
                        hideNonGameplayDescendants(node, hidden, false, pl);
                     }
                }
            }
        }
    }
}

static bool s_hidePlayerForCapture = false;

class $modify(GIFRecordPlayLayer, PlayLayer) {
    struct Fields {
        std::unique_ptr<EventListener<InvokeBindFilterV2>> m_listener;
        float m_frameTimer = 0.0f;
    };

    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
        s_hidePlayerForCapture = false;
        log::info("[GIFRecord] init() llamado para level {}", level ? level->m_levelID : 0);
        if (!PlayLayer::init(level, useReplay, dontCreateObjects)) return false;
        log::info("[GIFRecord] PlayLayer::init() exitoso, configurando keybinds...");
        if (Loader::get()->isModLoaded("geode.custom-keybinds")) {
            try {
                // Registration moved to main.cpp
                m_fields->m_listener = std::make_unique<EventListener<InvokeBindFilterV2>>(
                   [this](InvokeBindEventV2* e){ if (e->isDown()) this->captureScreenshot(); return ListenerResult::Propagate; },
                    InvokeBindFilterV2(this, "paimon.level_thumbnails/capture")
                );
            } catch (...) {}
        }
        
        log::info("[GIFRecord] init() completado exitosamente");
        return true;
    }
    
    void triggerRecapture(float dt) {
        this->captureScreenshot();
    }

    void captureScreenshot(CapturePreviewPopup* existingPopup = nullptr) {
        if (gCaptureInProgress.load()) return;
        gCaptureInProgress.store(true);

        auto* director = CCDirector::sharedDirector();
        if (!director || !this->m_level) { gCaptureInProgress.store(false); return; }
        auto* scene = director->getRunningScene();

        log::info("=== STARTING CAPTURE ===");
        // Debug: Log all direct children of PlayLayer
        {
            auto children = this->getChildren();
            if (children) {
                CCObject* obj;
                CCARRAY_FOREACH(children, obj) {
                    auto node = dynamic_cast<CCNode*>(obj);
                    if (node) {
                        std::string cls = typeid(*node).name();
                        std::string id = node->getID();
                        log::info("PlayLayer Child: Class='{}', ID='{}', Z={}, Vis={}", cls, id, node->getZOrder(), node->isVisible());
                    }
                }
            }
        }

        std::vector<CCNode*> hidden;
        
        // Estructura para guardar estado de visibilidad
        struct PlayerVisState {
            bool visible = true;
            bool regTrail = true;
            bool waveTrail = true;
            bool ghostTrail = true;
            bool vehicleGroundPart = true;
            bool robotFire = true;
            
            bool playerGroundPart = true;
            bool trailingPart = true;
            bool shipClickPart = true;
            bool ufoClickPart = true;
            bool robotBurstPart = true;
            bool dashPart = true;
            bool swingBurstPart1 = true;
            bool swingBurstPart2 = true;
            bool landPart0 = true;
            bool landPart1 = true;
            bool dashFireSprite = true;

            std::vector<std::pair<CCNode*, bool>> otherParticles;
        };
        PlayerVisState p1State, p2State;

        auto togglePlayer = [](PlayerObject* p, PlayerVisState& state, bool hide) {
            if (!p) return;
            
            auto toggle = [&](CCNode* node, bool& stateVar, bool hideNode) {
                if (node) {
                    if (hideNode) {
                        stateVar = node->isVisible();
                        node->setVisible(false);
                    } else {
                        node->setVisible(stateVar);
                    }
                }
            };

            if (hide) {
                state.visible = p->isVisible();
                p->setVisible(false);
                
                toggle(p->m_regularTrail, state.regTrail, true);
                toggle(p->m_waveTrail, state.waveTrail, true);
                toggle(p->m_ghostTrail, state.ghostTrail, true);
                toggle(p->m_vehicleGroundParticles, state.vehicleGroundPart, true);
                toggle(p->m_robotFire, state.robotFire, true);
                
                toggle(p->m_playerGroundParticles, state.playerGroundPart, true);
                toggle(p->m_trailingParticles, state.trailingPart, true);
                toggle(p->m_shipClickParticles, state.shipClickPart, true);
                toggle(p->m_ufoClickParticles, state.ufoClickPart, true);
                toggle(p->m_robotBurstParticles, state.robotBurstPart, true);
                toggle(p->m_dashParticles, state.dashPart, true);
                toggle(p->m_swingBurstParticles1, state.swingBurstPart1, true);
                toggle(p->m_swingBurstParticles2, state.swingBurstPart2, true);
                toggle(p->m_landParticles0, state.landPart0, true);
                toggle(p->m_landParticles1, state.landPart1, true);
                toggle(p->m_dashFireSprite, state.dashFireSprite, true);

                // Ocultar otros sistemas of partículas (nave, ufo, spider, swing, etc.)
                // que sean hijos directos del PlayerObject
                auto children = p->getChildren();
                if (children) {
                    CCObject* obj;
                    CCARRAY_FOREACH(children, obj) {
                        if (auto* node = dynamic_cast<CCNode*>(obj)) {
                            // Skip known members to avoid double toggling or issues
                            if (node == p->m_vehicleGroundParticles || 
                                node == p->m_robotFire ||
                                node == p->m_playerGroundParticles ||
                                node == p->m_trailingParticles ||
                                node == p->m_shipClickParticles ||
                                node == p->m_ufoClickParticles ||
                                node == p->m_robotBurstParticles ||
                                node == p->m_dashParticles ||
                                node == p->m_swingBurstParticles1 ||
                                node == p->m_swingBurstParticles2 ||
                                node == p->m_landParticles0 ||
                                node == p->m_landParticles1 ||
                                node == p->m_dashFireSprite) continue;
                            
                            // Only hide particles or sprites that might be effects
                            if (dynamic_cast<CCParticleSystemQuad*>(node) || dynamic_cast<CCSprite*>(node)) {
                                state.otherParticles.push_back({node, node->isVisible()});
                                node->setVisible(false);
                            }
                        }
                    }
                }

            } else {
                p->setVisible(state.visible);
                
                toggle(p->m_regularTrail, state.regTrail, false);
                toggle(p->m_waveTrail, state.waveTrail, false);
                toggle(p->m_ghostTrail, state.ghostTrail, false);
                toggle(p->m_vehicleGroundParticles, state.vehicleGroundPart, false);
                toggle(p->m_robotFire, state.robotFire, false);

                toggle(p->m_playerGroundParticles, state.playerGroundPart, false);
                toggle(p->m_trailingParticles, state.trailingPart, false);
                toggle(p->m_shipClickParticles, state.shipClickPart, false);
                toggle(p->m_ufoClickParticles, state.ufoClickPart, false);
                toggle(p->m_robotBurstParticles, state.robotBurstPart, false);
                toggle(p->m_dashParticles, state.dashPart, false);
                toggle(p->m_swingBurstParticles1, state.swingBurstPart1, false);
                toggle(p->m_swingBurstParticles2, state.swingBurstPart2, false);
                toggle(p->m_landParticles0, state.landPart0, false);
                toggle(p->m_landParticles1, state.landPart1, false);
                toggle(p->m_dashFireSprite, state.dashFireSprite, false);

                for (auto& pair : state.otherParticles) {
                    pair.first->setVisible(pair.second);
                }
                state.otherParticles.clear();
            }
        };

        if (s_hidePlayerForCapture) {
            togglePlayer(this->m_player1, p1State, true);
            togglePlayer(this->m_player2, p2State, true);
        }
        
        // Estrategia simplificada: PlayLayer ES el nodo de juego.
        // Todo lo que sea UI dentro de PlayLayer debe ser ocultado.
        // Usamos hideNonGameplayDescendants directamente sobre 'this'.
        // checkZ=true para ocultar cualquier cosa con Z>=10 en el primer nivel (hijos directos)
        log::info("[Capture] Iniciando limpieza recursiva de UI en PlayLayer");
        hideNonGameplayDescendants(this, hidden, true, this);

        // Depuración mínima opcional
        if (!hidden.empty()) log::info("[Capture] Ocultados {} nodos (recursivo)", hidden.size());

        auto hiddenCopy = hidden; // se copia la lista para restaurarla luego
        int levelID = this->m_level->m_levelID;
        
        // Not pasar nodo específico - usar captura of framebuffer completo
        // pero with the overlays ya ocultos by the código anterior
        // Esto captura toda la escena renderizada sin UI
        log::info("[Capture] Capturando PlayLayer usando RenderTexture");
        
        // Ocultar explícitamente m_uiLayer if exists and not fue capturado by the recursivo
        if (this->m_uiLayer && this->m_uiLayer->isVisible()) {
            this->m_uiLayer->setVisible(false);
            hidden.push_back(this->m_uiLayer);
            log::info("[Capture] Ocultando m_uiLayer explícitamente");
        }

        // Backup loop simplificado (ya cubierto por el recursivo, pero por seguridad)
        CCObject* obj = nullptr;
        CCARRAY_FOREACH(this->getChildren(), obj) {
            auto* node = dynamic_cast<CCNode*>(obj);
            if (!node) continue;
            if (node == this->m_uiLayer) continue; 
            
            // Solo verificamos si es visible y si isNonGameplayOverlay dice que es UI
            // Pasamos checkZ=true porque estamos iterando hijos directos
            if (node->isVisible() && isNonGameplayOverlay(node, true)) {
                // Check if ya está in the lista hidden to not duplicar
                bool alreadyHidden = false;
                for(auto* h : hidden) if(h == node) { alreadyHidden = true; break; }
                
                if (!alreadyHidden) {
                    node->setVisible(false);
                    hidden.push_back(node);
                    log::info("[Capture] Ocultando nodo UI (Backup Loop): ID='{}', Class='{}', Z={}", 
                        node->getID(), typeid(*node).name(), node->getZOrder());
                }
            }
        }

        // 2. Capture
        auto view = CCEGLView::sharedOpenGLView();
        auto screenSize = view->getFrameSize();
        int width = static_cast<int>(screenSize.width);
        int height = static_cast<int>(screenSize.height);

        std::unique_ptr<uint8_t[]> data;
        bool needsVerticalFlip = true;

        // MODO ESTÁNDAR: Usar RenderTexture (FBO separado)
        RenderTexture rt(width, height);
        rt.begin();
        this->visit();
        rt.end();
        data = rt.getData();
        
        needsVerticalFlip = true; // Necesita flip porque glReadPixels es Bottom-Up

        // 5. Restaurar nodos ocultos with validación
        for (auto* n : hiddenCopy) {
            if (n) {
                try {
                    n->setVisible(true);
                } catch (...) {
                    // Ignorar si el nodo ya fue destruido
                }
            }
        }
        
        // Restaurar visibilidad de jugadores
        if (s_hidePlayerForCapture) {
            togglePlayer(this->m_player1, p1State, false);
            togglePlayer(this->m_player2, p2State, false);
        }
        
        if (!data) { 
            gCaptureInProgress.store(false); 
            return; 
        }

        // Flip vertical del buffer (glReadPixels lee de abajo hacia arriba)
        // Only if es necesario (Modo Estándar)
        if (needsVerticalFlip) {
            int rowSize = width * 4;
            std::vector<uint8_t> tempRow(rowSize);
            uint8_t* buffer = data.get();
            for (int y = 0; y < height / 2; ++y) {
                uint8_t* topRow = buffer + y * rowSize;
                uint8_t* bottomRow = buffer + (height - y - 1) * rowSize;
                std::memcpy(tempRow.data(), topRow, rowSize);
                std::memcpy(topRow, bottomRow, rowSize);
                std::memcpy(bottomRow, tempRow.data(), rowSize);
            }
        }

        // Create CCTexture2D from data (RGBA8888)
        auto* tex = new CCTexture2D();
        tex->initWithData(data.get(), kCCTexture2DPixelFormat_RGBA8888, width, height, CCSize(width, height));
        tex->autorelease();
        tex->retain(); // Retain for popup

        // Convert data to shared_ptr for popup (RGBA)
        std::shared_ptr<uint8_t> rgba(new uint8_t[width * height * 4], std::default_delete<uint8_t[]>());
        memcpy(rgba.get(), data.get(), width * height * 4);
        
        bool pausedByPopup = false;
        if (!this->m_isPaused) { this->pauseGame(true); pausedByPopup = true; }
        
        // Si ya existe un popup, actualizamos su contenido y lo mostramos
        if (existingPopup) {
            existingPopup->updateContent(tex, rgba, width, height);
            existingPopup->setVisible(true);
            tex->release(); // updateContent hace retain, así that liberamos nuestra referencia
            return;
        }

        // Helper to check moderator status (copied from PauseLayer)
        auto isUserModerator = []() -> bool {
            try {
                auto modDataPath = Mod::get()->getSaveDir() / "moderator_verification.dat";
                if (std::filesystem::exists(modDataPath)) {
                    std::ifstream modFile(modDataPath, std::ios::binary);
                    if (modFile) {
                        time_t timestamp{};
                        modFile.read(reinterpret_cast<char*>(&timestamp), sizeof(timestamp));
                        modFile.close();
                        auto now = std::chrono::system_clock::now();
                        auto fileTime = std::chrono::system_clock::from_time_t(timestamp);
                        auto daysDiff = std::chrono::duration_cast<std::chrono::hours>(now - fileTime).count() / 24;
                        if (daysDiff < 30) {
                            return true;
                        }
                    }
                }
                return Mod::get()->getSavedValue<bool>("is-verified-moderator", false);
            } catch (...) {
                return false;
            }
        };

        bool isMod = isUserModerator();

        auto* popup = CapturePreviewPopup::create(
            tex, 
            levelID, 
            rgba, 
            width, 
            height, 
            [levelID, pausedByPopup](bool okSave, int levelIDAccepted, std::shared_ptr<uint8_t> buf, int W, int H, std::string mode, std::string replaceId){
            try {
                // CRÍTICO: Resetear the bandera inmediatamente cuando the popup se cierra
                // Esto permite capturas subsecuentes sin importar si las operaciones async completan
                gCaptureInProgress.store(false);
                
                // Intentar despausar el juego si fue pausado por el popup
                // Solo si no hay un PauseLayer activo y el PlayLayer sigue existiendo
                if (pausedByPopup) {
                    auto* pl = PlayLayer::get();
                    if (pl && pl->m_isPaused) {
                        bool hasPause = false;
                        if (auto* sc = CCDirector::sharedDirector()->getRunningScene()) {
                            CCArrayExt<CCNode*> children(sc->getChildren());
                            for (auto child : children) { 
                                if (dynamic_cast<PauseLayer*>(child)) { 
                                    hasPause = true; 
                                    break; 
                                } 
                            }
                        }
                        if (!hasPause) {
                            if (auto* d = CCDirector::sharedDirector()) {
                                if (d->getScheduler() && d->getActionManager()) {
                                    d->getScheduler()->resumeTarget(pl);
                                    d->getActionManager()->resumeTarget(pl);
                                    pl->m_isPaused = false;
                                }
                            }
                        }
                    }
                }

                // If the usuario aceptó, guardar localmente and comprobar rol before of upload
                if (okSave && levelIDAccepted > 0 && buf) {
                    // Respetar the ajuste: guardar only if está habilitado
                    // LocalThumbs::get().saveFromRGBA(levelIDAccepted, buf.get(), static_cast<uint32_t>(W), static_cast<uint32_t>(H));

                    // Extraer siempre los colores dominantes para reutilizarlos en gradientes
                    // DominantColors espera RGB, así that necesitamos convertir or actualizar DominantColors
                    // Por ahora, convertimos RGBA a RGB temporalmente para DominantColors y PNG
                    
                    std::vector<uint8_t> rgbData(static_cast<size_t>(W) * static_cast<size_t>(H) * 3);
                    const uint8_t* src = buf.get();
                    for(size_t i=0; i < static_cast<size_t>(W)*H; ++i) {
                        rgbData[i*3+0] = src[i*4+0];
                        rgbData[i*3+1] = src[i*4+1];
                        rgbData[i*3+2] = src[i*4+2];
                    }

                    auto pair = DominantColors::extract(rgbData.data(), W, H);
                    ccColor3B A{pair.first.r, pair.first.g, pair.first.b};
                    ccColor3B B{pair.second.r, pair.second.g, pair.second.b};
                    LevelColors::get().set(levelIDAccepted, A, B);

                    // Convertir a PNG (usando los datos RGBA originales para mantener calidad si ImageConverter lo soporta, 
                    // pero ImageConverter::rgbToPng espera vector. Podemos pasarle el vector RGBA)
                    
                    std::vector<uint8_t> rgbaData(static_cast<size_t>(W) * static_cast<size_t>(H) * 4);
                    memcpy(rgbaData.data(), buf.get(), rgbaData.size());
                    
                    std::vector<uint8_t> pngData;
                    if (!ImageConverter::rgbToPng(rgbaData, static_cast<uint32_t>(W), static_cast<uint32_t>(H), pngData)) {
                        Notification::create(Localization::get().getString("capture.save_png_error"), NotificationIcon::Error)->show();
                    } else {
                        // Resolver el nombre de usuario
                        std::string username;
                        int accountID = 0;
                        if (auto* gm = GameManager::sharedState()) {
                            username = gm->m_playerName;
                            accountID = gm->m_playerUserID;
                        }
                        if (username.empty()) username = "unknown";

                        if (accountID <= 0) {
                            Notification::create("Tienes que tener cuenta para subir", NotificationIcon::Error)->show();
                            return;
                        }

                        // Check if the usuario es moderator before of upload
                        Notification::create(Localization::get().getString("capture.verifying"), NotificationIcon::Info)->show();
                        // Nota: the validación of accountID se eliminó; Geode not lo expone
                        // The servidor validará in base al nombre of usuario and otras comprobaciones
                        ThumbnailAPI::get().checkModeratorAccount(username, accountID, [levelIDAccepted, pngData, username, mode, replaceId](bool approved, bool isAdmin) {
                            bool allowModeratorFlow = approved;
                            if (allowModeratorFlow) {
                                // Usuario verificado como moderador: subir al servidor
                                log::info("[PlayLayer] Usuario verificado como moderador, subiendo thumbnail");
                                Notification::create(Localization::get().getString("capture.uploading"), NotificationIcon::Info)->show();
                                ThumbnailAPI::get().uploadThumbnail(levelIDAccepted, pngData, username, mode, replaceId, [levelIDAccepted](bool success, const std::string& msg){
                                    if (success) {
                                        Notification::create(Localization::get().getString("capture.upload_success"), NotificationIcon::Success)->show();
                                        PendingQueue::get().removeForLevel(levelIDAccepted);
                                    } else {
                                        Notification::create(Localization::get().getString("capture.upload_error") + (msg.empty() ? std::string("") : (" (" + msg + ")")), NotificationIcon::Error)->show();
                                    }
                                });
                            } else {
                                // Usuario NO es moderador: subir sugerencia al servidor Y encolar localmente
                                log::info("[PlayLayer] Usuario no es moderador, subiendo sugerencia y encolando");
                                
                                // Primero subir al servidor
                                Notification::create(Localization::get().getString("capture.uploading_suggestion"), NotificationIcon::Info)->show();
                                ThumbnailAPI::get().uploadSuggestion(levelIDAccepted, pngData, username, [levelIDAccepted, username](bool success, const std::string& msg) {
                                    if (success) {
                                        log::info("[PlayLayer] Sugerencia subida exitosamente");
                                        // The sugerencias siempre van a the categoría Verify
                                        // Not podemos Check isCreator without the puntero of the nivel, usar false
                                        PendingQueue::get().addOrBump(levelIDAccepted, PendingCategory::Verify, username, {}, false);
                                        Notification::create(Localization::get().getString("capture.suggested"), NotificationIcon::Success)->show();
                                    } else {
                                        log::error("[PlayLayer] Error subiendo sugerencia: {}", msg);
                                        Notification::create(Localization::get().getString("capture.upload_error") + (msg.empty() ? std::string("") : (" (" + msg + ")")), NotificationIcon::Error)->show();
                                    }
                                });
                            }
                        });
                    }
                }
                // gCaptureInProgress ya se reseteó al inicio of the callback
            } catch (const std::exception& e) {
                log::error("[Capture] Exception en callback de captura: {}", e.what());
                gCaptureInProgress.store(false);
            } catch (...) {
                log::error("[Capture] Exception desconocida en callback de captura");
                gCaptureInProgress.store(false);
            }
        },
        [this](bool hidePlayer, CapturePreviewPopup* popup) {
            s_hidePlayerForCapture = hidePlayer;
            
            // Ocultar el popup para que no salga en la captura
            if (popup) popup->setVisible(false);

            gCaptureInProgress = false;
            Loader::get()->queueInMainThread([this, popup]() {
                this->captureScreenshot(popup);
            });
        },
        s_hidePlayerForCapture
        );
        if (popup) { 
            if (existingPopup) {
                // Si ya existe un popup, actualizamos su contenido en lugar de mostrar uno nuevo
                // Pero espera, 'popup' aquí es the NUEVO popup creado by create().
                // If existingPopup != nullptr, Not deberíamos haber creado uno nuevo.
                // Necesitamos reestructurar esto.
            } else {
                popup->show(); 
            }
            try {
                tex->release();
            } catch (...) {
                log::error("[Capture] Error releasing texture after showing popup");
            }
        }
        else { 
            try {
                tex->release();
            } catch (...) {
                log::error("[Capture] Error releasing texture when popup creation failed");
            }
            gCaptureInProgress.store(false); 
        }
    }
};


