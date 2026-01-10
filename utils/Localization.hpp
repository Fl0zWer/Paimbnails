#pragma once
#include <string>
#include <unordered_map>
#include <Geode/Geode.hpp>

using namespace geode::prelude;

class Localization {
public:
    enum class Language {
        SPANISH,
        ENGLISH
    };

    static Localization& get() {
        static Localization instance;
        return instance;
    }

    void setLanguage(Language lang) {
        m_currentLanguage = lang;
        // Save as string to match mod.json
        Mod::get()->setSavedValue("language", std::string(lang == Language::SPANISH ? "spanish" : "english"));
    }

    Language getLanguage() const {
        return m_currentLanguage;
    }

    std::string getString(const std::string& key) const {
        auto& translations = (m_currentLanguage == Language::SPANISH) ? m_spanish : m_english;
        auto it = translations.find(key);
        if (it != translations.end()) {
            return it->second;
        }
        return key; // Fallback to key if not found
    }

    void loadFromSettings() {
        // Read as string to match mod.json
        std::string langStr = Mod::get()->getSavedValue<std::string>("language", "english");
        if (langStr == "spanish") {
            m_currentLanguage = Language::SPANISH;
        } else {
            m_currentLanguage = Language::ENGLISH;
        }
    }

private:
    Localization() {
        loadFromSettings();
        initTranslations();
    }

    void initTranslations() {
        // Spanish translations
        m_spanish = {
            // CapturePreviewPopup
            {"preview.title", "Vista previa"},
            {"preview.borders_removed", "Ya se eliminaron los bordes"},
            {"preview.no_borders", "No se detectaron bordes negros"},
            {"preview.borders_deleted", "Bordes eliminados"},
            {"preview.fill_mode_active", "Rellenar (activo)"},
            {"preview.fit_mode_active", "Ajustar (activo)"},
            {"preview.player_toggle_error", "No se puede alternar visibilidad del jugador"},
            {"preview.no_image", "No hay imagen para descargar"},
            {"preview.folder_error", "Error al crear carpeta de descargas"},
            {"preview.downloaded", "Miniatura descargada!"},
            {"preview.save_error", "Error al guardar archivo"},
            {"preview.process_error", "Error al procesar imagen"},

            // PlayLayer & PauseLayer
            {"capture.action_name", "Capturar Miniatura"},
            {"capture.action_desc", "Toma una captura del nivel actual"},
            {"capture.error", "Error al capturar miniatura"},
            {"capture.process_error", "Error al procesar miniatura"},
            {"capture.save_png_error", "Error al guardar PNG"},
            {"capture.read_png_error", "Error al leer PNG"},
            {"capture.upload_error", "Error al subir miniatura"},
            {"capture.uploading", "Subiendo miniatura al servidor..."},
            {"capture.upload_success", "Miniatura subida exitosamente!"},
            {"capture.suggested", "Miniatura sugerida"},
            {"capture.verifying", "Verificando permisos..."},
            {"capture.uploading_suggestion", "Subiendo sugerencia..."},

            // PauseLayer specific
            {"pause.no_local_thumb", "No hay miniatura local para subir"},
            {"pause.only_moderators", "Solo moderadores pueden subir miniaturas"},
            {"pause.access_error", "Error al acceder a miniatura local"},
            {"pause.read_error", "Error al leer miniatura local"},
            {"pause.gif_disabled", "Grabación GIF deshabilitada temporalmente"},
            {"pause.playlayer_error", "Error: PlayLayer no disponible"},
            {"pause.capture_error", "Error al iniciar captura"},
            {"pause.gif_open_error", "Error: No se pudo abrir el GIF"},
            {"pause.gif_read_error", "Error: No se pudo leer el GIF"},
            {"pause.gif_texture_error", "Error: No se pudo crear textura del GIF"},
            {"pause.gif_uploading", "Subiendo GIF al servidor..."},
            {"pause.gif_uploaded", "GIF subido correctamente"},
            {"pause.gif_upload_error", "No se pudo subir el GIF"},
            {"pause.gif_process_error", "Error al procesar GIF"},
            {"pause.file_open_error", "Error: No se pudo abrir el archivo"},
            {"pause.png_invalid", "Error: Archivo PNG invalido"},
            {"pause.process_thumbnail_error", "Failed to process thumbnail"},

            // ProfilePage
            {"profile.username_error", "No se pudo obtener tu nombre de usuario"},
            {"profile.verified", "Verificado"},
            {"profile.verified_msg", "Eres un <cg>moderador aprobado</c>! Ahora puedes subir y verificar miniaturas."},
            {"profile.not_verified", "No verificado"},
            {"profile.not_verified_msg", "No estas en la lista de moderadores aprobados."},
            {"profile.image_open_error", "No se pudo abrir imagen"},
            {"profile.texture_error", "No se pudo crear textura"},
            {"profile.saved", "Miniatura de perfil guardada"},
            {"profile.no_image_selected", "No se selecciono imagen"},
            {"profile.invalid_image_data", "Datos de imagen invalidos"},
            {"profile.access_denied", "Acceso denegado"},
            {"profile.moderators_only", "Solo los moderadores pueden acceder al centro de verificacion."},

            // LevelInfoLayer
            {"level.title", "Miniatura del nivel"},
            {"level.no_thumbnail", "No hay miniatura"},
            {"level.open_error", "Error al abrir miniatura"},
            {"level.read_error", "Error al leer miniatura"},
            {"level.create_error", "Error al crear imagen"},
            {"level.save_error", "Error al guardar imagen"},
            {"level.saved", "Imagen guardada correctamente"},
            {"level.no_local", "No hay miniatura local"},
            {"level.cant_open", "No se pudo abrir la miniatura"},
            {"level.corrupt", "Miniatura corrupta"},
            {"level.report_button", "Reportar"},
            {"level.delete_button", "Borrar miniatura"},
            {"level.accept_button", "Aceptar"},
            {"level.download_button", "Descargar"},

            // VerificationQueuePopup
            {"queue.title", "Centro de verificacion"},
            {"queue.verify_tab", "verificar thumbnails"},
            {"queue.update_tab", "actualizacion"},
            {"queue.report_tab", "reportes"},
            {"queue.no_items", "No hay elementos"},
            {"queue.open_button", "Abrir"},
            {"queue.accept_button", "Aceptar"},
            {"queue.reject_button", "Rechazar"},
            {"queue.view_report", "Ver reporte"},
            {"queue.report_reason", "Motivo del reporte"},
            {"queue.close", "Cerrar"},
            {"queue.no_local", "No hay miniatura local"},
            {"queue.cant_open", "No se pudo abrir miniatura"},
            {"queue.corrupt", "Miniatura corrupta"},
            {"queue.read_error", "Error al leer miniatura"},
            {"queue.create_error", "Error al crear imagen"},
            {"queue.png_error", "Error al generar PNG"},
            {"queue.png_read_error", "Error al leer PNG"},
            {"queue.accepting", "Aceptando y subiendo al servidor..."},
            {"queue.accepted", "Miniatura aceptada y sincronizada"},
            {"queue.accept_error", "Error al sincronizar con servidor"},
            {"queue.rejecting", "Rechazando en servidor..."},
            {"queue.rejected", "Miniatura rechazada y sincronizada"},
            {"queue.reject_error", "Error al sincronizar rechazo"},

            // BulkUploadPopup
            {"bulk.select_folder", "Seleccionar Carpeta"},
            {"bulk.start_upload", "Iniciar Subida"},
            {"bulk.upload_in_progress", "Ya hay una subida en progreso"},
            {"bulk.no_thumbnails", "No hay thumbnails para subir"},
            {"bulk.complete_title", "Subida completa"},
            {"bulk.complete_msg", "Se subieron {0} de {1} thumbnails exitosamente."},

            // AddModeratorPopup
            {"addmod.enter_username", "Ingresa un nombre de usuario"},
            {"addmod.success_title", "Agregado"},
            {"addmod.success_msg", "Moderador agregado exitosamente"},
            {"addmod.error_title", "Error"},
            {"addmod.error_msg", "No se pudo agregar al moderador"},
            {"addmod.title", "Agregar Moderador"},
            {"addmod.add_btn", "Agregar"},
            {"addmod.enter_username_label", "Ingresa el nombre de usuario:"},
            {"general.cancel", "Cancelar"},

            // BulkUploadPopup
            {"bulk.title", "Subida Masiva"},
            {"bulk.select_folder_label", "Selecciona una carpeta con thumbnails"},
            {"bulk.progress_label", "0 / 0 thumbnails"},
            {"bulk.info_text", "Los archivos deben tener el formato:\nlevelID.png (ej: 12345.png)\n\nSolo se subirán thumbnails de\nniveles que no tengan ya uno."},
            {"bulk.scanning", "Escaneando carpeta..."},

            // GIFUploadPopup
            {"gif.upload.title", "GIF Grabado"},
            {"gif.label", "GIF"},

            // SetDailyWeeklyPopup
            {"daily.title", "Establecer Nivel Destacado"},
            {"daily.set_daily", "Poner Daily"},
            {"daily.set_weekly", "Poner Weekly"},
            {"daily.success_daily_title", "Exito"},
            {"daily.success_daily_msg", "Nivel establecido como Daily!"},
            {"daily.error_daily_title", "Error"},
            {"daily.error_daily_msg", "Fallo al establecer Daily."},
            {"daily.success_weekly_title", "Exito"},
            {"daily.success_weekly_msg", "Nivel establecido como Weekly!"},
            {"daily.error_weekly_title", "Error"},
            {"daily.error_weekly_msg", "Fallo al establecer Weekly."},

            // TaskReadyToast
            {"toast.task_ready", "Tarea lista"},

            // ThumbnailViewPopup
            {"thumbview.title", "Miniatura"},

            // VerificationQueuePopup
            {"queue.banned_btn", "Baneados"},
            {"queue.level_id", "Nivel {}"},
            {"queue.claimed_by_you", "Reclamado por ti"},
            {"queue.claimed_by_user", "Reclamado por {}"},
            {"queue.view_btn", "Ver"},
            {"queue.view_thumb", "Ver Miniatura"},
            {"queue.claim_btn", "Reclamar"},
            {"queue.unclaim_btn", "Desreclamar"},
            {"queue.accept_btn", "Aceptar"},
            {"queue.reject_btn", "Rechazar"},
            {"queue.verified_by", "Verificado por {}"},
            {"queue.reported_by", "Reportado por {}"},
            {"queue.reason", "Motivo: {}"},
            {"queue.ignore_btn", "Ignorar"},
            {"queue.delete_btn", "Borrar"},
            {"queue.keep_btn", "Mantener"},
            {"queue.claiming", "Reclamando nivel..."},
            {"queue.claimed", "Nivel reclamado"},
            {"queue.claim_error", "Error: {}"},

            // ButtonEditOverlay
            {"edit.accept", "Aceptar"},
            {"edit.reset", "Reiniciar"},
            {"edit.scale", "Escala:"},
            {"edit.opacity", "Opacidad:"},

            // LeaderboardsLayer
            {"leaderboard.daily", "Diario"},
            {"leaderboard.weekly", "Semanal"},
            {"leaderboard.all_time", "Global"},
            {"leaderboard.creators", "Creadores"},
            {"leaderboard.error", "Error"},
            {"leaderboard.read_error", "Error al leer respuesta"},
            {"leaderboard.load_error", "Error al cargar leaderboard: {}"},
            {"leaderboard.parse_error", "Error al procesar JSON"},
            {"leaderboard.server_error", "Error del servidor"},
            {"leaderboard.invalid_format", "Formato de datos inválido"},
            {"leaderboard.loading", "Cargando..."},
            {"leaderboard.unknown", "Desconocido"},
            {"mods.title", "Moderadores de Paimbnails"},
            {"shop.title", "Tienda Paimon"},
            {"shop.buy_coins", "Comprar 100"},
            {"shop.upload_banner", "Subir Banner"},
            {"shop.premium_banners", "Banners Premium"},
            {"shop.page", "Página {}/{}"},
            {"shop.store_title", "Tienda Paimon"},
            {"shop.tab_shop", "Tienda"},
            {"shop.tab_tasks", "Tareas"},
            {"leaderboard.no_refreshes", "No more refreshes available today!"},
            {"leaderboard.task_complete", "Task completed!"},
            {"leaderboard.no_gamemanager", "No se pudo obtener GameManager"},
            {"leaderboard.empty_username", "Nombre de usuario vacío"},
            {"leaderboard.no_image", "No se seleccionó imagen"},
            {"leaderboard.png_open_error", "No se pudo abrir PNG"},
            {"leaderboard.profile_saved_local", "Profile saved locally (server upload disabled)"},
            {"leaderboard.uploading_profile", "Subiendo perfil..."},
            {"leaderboard.profile_uploaded", "Perfil subido"},
            {"leaderboard.profile_error", "Error al subir perfil"},
            {"leaderboard.unknown_error", "Error desconocido"},
            {"leaderboard.synced", "Synced with server!"},

            // GIFRecordSettingsPopup
            {"gif.start", "Iniciar"},

            // PaimonShopLayer
            {"shop.admin_required", "Admin privileges required"},
            {"shop.file_open_error", "No se pudo abrir el archivo"},
            {"shop.uploading_banner", "Subiendo banner..."},
            {"shop.banner_uploaded", "subido correctamente"},
            {"shop.banner_error", "No se pudo subir el banner"},

            // PaimonCoinManager
            {"coins.already_owned", "You already own this banner!"},
            {"coins.purchased", "Banner purchased successfully!"},
            {"coins.not_enough", "Not enough Paimon Coins!"},

            // General
            {"general.error", "Error"},
            {"general.ok", "OK"},
            {"general.close", "Cerrar"},

            // Missing keys added
            {"level.no_thumbnail_text", "No hay miniatura"},
            {"level.saving_mod_folder", "Guardando en carpeta del mod..."},
            {"level.error_prefix", "Error: "},
            {"level.delete_moderator_only", "Solo moderadores pueden borrar miniaturas"},
            {"level.deleting_server", "Borrando miniatura del servidor..."},
            {"level.deleted_server", "Miniatura eliminada del servidor"},
            {"level.delete_error", "Error al borrar: "},
            {"level.accepting", "Aceptando miniatura..."},
            {"level.accepted", "¡Miniatura aceptada!"},
            {"level.accept_error", "Error al aceptar: "},
            {"level.no_local_thumb", "No hay miniatura local"},
            {"level.png_error", "Error al generar PNG"},
            {"level.saved_local_server_disabled", "Miniatura guardada localmente (servidor deshabilitado)"},

            // Report Popup
            {"report.title", "Reportar Miniatura"},
            {"report.cancel", "Cancelar"},
            {"report.send", "Enviar"},
            {"report.placeholder", "Escribe aqui..."},
            {"report.empty_reason", "Debes especificar una razon"},
            {"report.sent_synced", "Reporte enviado y sincronizado: "},
            {"report.saved_local", "Reporte guardado localmente (sin conexión)"},

            // PauseLayer
            {"pause.gif_not_supported", "Archivos GIF no soportados en esta plataforma"},
            {"pause.process_image_error", "Error: No se pudo procesar la imagen"},
            {"pause.create_texture_error", "Error: No se pudo crear textura"},
            {"pause.init_texture_error", "Error: No se pudo inicializar textura"},
            {"pause.username_error", "Error: No se pudo obtener nombre de usuario"},
            {"pause.gif_recording_started", "Grabación GIF iniciada"},

            // Ban System
            {"ban.list.title", "Baneados"},
            {"ban.list.loading", "Cargando..."},
            {"ban.list.empty", "Sin baneos"},
            {"ban.list.unban_btn", "Desbanear"},
            {"ban.info.no_info", "Sin informacion disponible."},
            {"ban.info.reason", "Razon"},
            {"ban.info.by", "Por"},
            {"ban.info.date", "Fecha"},
            {"ban.info.title", "Detalles del Baneo"},
            {"ban.unban.title", "Desbanear Usuario"},
            {"ban.unban.confirm", "¿Estas seguro de desbanear a <cy>{}</c>?"},
            {"ban.unban.success", "Usuario desbaneado"},
            {"ban.unban.error", "Error al desbanear"},
            {"ban.popup.title", "Banear Usuario"},
            {"ban.popup.user", "Usuario: {}"},
            {"ban.popup.placeholder", "Razon del baneo..."},
            {"ban.popup.ban_btn", "Banear"},
            {"ban.popup.enter_reason", "Ingresa una razon"},
            {"ban.popup.success", "Usuario baneado"},
            {"ban.popup.error", "Error al banear"},
            {"ban.profile.mod_only", "Solo moderadores/admins"},
            {"ban.profile.self_ban", "No puedes banearte"},
            {"ban.profile.read_error", "No se pudo leer el usuario"}
        };

        // English translations
        m_english = {
            // CapturePreviewPopup
            {"preview.title", "Preview"},
            {"preview.borders_removed", "Borders already removed"},
            {"preview.no_borders", "No black borders detected"},
            {"preview.borders_deleted", "Borders deleted"},
            {"preview.fill_mode_active", "Fill (active)"},
            {"preview.fit_mode_active", "Fit (active)"},
            {"preview.player_toggle_error", "Cannot toggle player visibility"},
            {"preview.no_image", "No image to download"},
            {"preview.folder_error", "Failed to create download folder"},
            {"preview.downloaded", "Thumbnail downloaded!"},
            {"preview.save_error", "Failed to save file"},
            {"preview.process_error", "Failed to process image"},

            // PlayLayer & PauseLayer
            {"capture.action_name", "Capture Thumbnail"},
            {"capture.action_desc", "Takes a screenshot of the current level"},
            {"capture.error", "Failed to capture thumbnail"},
            {"capture.process_error", "Failed to process thumbnail"},
            {"capture.save_png_error", "Failed to save PNG"},
            {"capture.read_png_error", "Failed to read PNG"},
            {"capture.upload_error", "Failed to upload thumbnail"},
            {"capture.uploading", "Uploading thumbnail to server..."},
            {"capture.upload_success", "Thumbnail uploaded successfully!"},
            {"capture.suggested", "Thumbnail suggested"},
            {"capture.verifying", "Verifying permissions..."},
            {"capture.uploading_suggestion", "Uploading suggestion..."},

            // PauseLayer specific
            {"pause.no_local_thumb", "No local thumbnail to upload"},
            {"pause.only_moderators", "Only moderators can upload thumbnails"},
            {"pause.access_error", "Failed to access local thumbnail"},
            {"pause.read_error", "Failed to read local thumbnail"},
            {"pause.gif_disabled", "GIF recording temporarily disabled"},
            {"pause.playlayer_error", "Error: PlayLayer not available"},
            {"pause.capture_error", "Failed to start capture"},
            {"pause.gif_open_error", "Error: Could not open GIF"},
            {"pause.gif_read_error", "Error: Could not read GIF"},
            {"pause.gif_texture_error", "Error: Could not create GIF texture"},
            {"pause.gif_uploading", "Uploading GIF to server..."},
            {"pause.gif_uploaded", "GIF uploaded successfully"},
            {"pause.gif_upload_error", "Failed to upload GIF"},
            {"pause.gif_process_error", "Failed to process GIF"},
            {"pause.file_open_error", "Error: Could not open file"},
            {"pause.png_invalid", "Error: Invalid PNG file"},
            {"pause.process_thumbnail_error", "Failed to process thumbnail"},

            // ProfilePage
            {"profile.username_error", "Could not get your username"},
            {"profile.verified", "Verified"},
            {"profile.verified_msg", "You are an <cg>approved moderator</c>! You can now upload and verify thumbnails."},
            {"profile.not_verified", "Not Verified"},
            {"profile.not_verified_msg", "You are not on the approved moderators list."},
            {"profile.image_open_error", "Could not open image"},
            {"profile.texture_error", "Could not create texture"},
            {"profile.saved", "Profile thumbnail saved"},
            {"profile.no_image_selected", "No image selected"},
            {"profile.invalid_image_data", "Invalid image data"},
            {"profile.access_denied", "Access denied"},
            {"profile.moderators_only", "Only moderators can access the verification center."},

            // LevelInfoLayer
            {"level.title", "Level Thumbnail"},
            {"level.no_thumbnail", "No thumbnail"},
            {"level.open_error", "Failed to open thumbnail"},
            {"level.read_error", "Failed to read thumbnail"},
            {"level.create_error", "Failed to create image"},
            {"level.save_error", "Failed to save image"},
            {"level.saved", "Image saved successfully"},
            {"level.no_local", "No local thumbnail"},
            {"level.cant_open", "Could not open thumbnail"},
            {"level.corrupt", "Corrupt thumbnail"},
            {"level.report_button", "Report"},
            {"level.delete_button", "Delete thumbnail"},
            {"level.accept_button", "Accept"},
            {"level.download_button", "Download"},

            // VerificationQueuePopup
            {"queue.title", "Verification center"},
            {"queue.verify_tab", "verify thumbnails"},
            {"queue.update_tab", "update"},
            {"queue.report_tab", "reports"},
            {"queue.no_items", "No items"},
            {"queue.open_button", "Open"},
            {"queue.accept_button", "Accept"},
            {"queue.reject_button", "Reject"},
            {"queue.view_report", "View report"},
            {"queue.report_reason", "Report reason"},
            {"queue.close", "Close"},
            {"queue.no_local", "No local thumbnail"},
            {"queue.cant_open", "Could not open thumbnail"},
            {"queue.corrupt", "Corrupt thumbnail"},
            {"queue.read_error", "Failed to read thumbnail"},
            {"queue.create_error", "Failed to create image"},
            {"queue.png_error", "Failed to generate PNG"},
            {"queue.png_read_error", "Failed to read PNG"},
            {"queue.accepting", "Accepting and uploading to server..."},
            {"queue.accepted", "Thumbnail accepted and synced"},
            {"queue.accept_error", "Failed to sync with server"},
            {"queue.rejecting", "Rejecting on server..."},
            {"queue.rejected", "Thumbnail rejected and synced"},
            {"queue.reject_error", "Failed to sync rejection"},

            // BulkUploadPopup
            {"bulk.select_folder", "Select Folder"},
            {"bulk.start_upload", "Start Upload"},
            {"bulk.upload_in_progress", "Upload already in progress"},
            {"bulk.no_thumbnails", "No thumbnails to upload"},
            {"bulk.complete_title", "Upload complete"},
            {"bulk.complete_msg", "Successfully uploaded {0} of {1} thumbnails."},

            // AddModeratorPopup
            {"addmod.enter_username", "Enter a username"},
            {"addmod.success_title", "Added"},
            {"addmod.success_msg", "Moderator added successfully"},
            {"addmod.error_title", "Error"},
            {"addmod.error_msg", "Could not add moderator"},
            {"addmod.title", "Add Moderator"},
            {"addmod.add_btn", "Add"},
            {"addmod.enter_username_label", "Enter username:"},
            {"general.cancel", "Cancel"},

            // BulkUploadPopup
            {"bulk.title", "Bulk Upload"},
            {"bulk.select_folder_label", "Select a folder with thumbnails"},
            {"bulk.progress_label", "0 / 0 thumbnails"},
            {"bulk.info_text", "Files must have the format:\nlevelID.png (e.g. 12345.png)\n\nOnly thumbnails for levels\nwithout one will be uploaded."},
            {"bulk.scanning", "Scanning folder..."},

            // GIFUploadPopup
            {"gif.upload.title", "Recorded GIF"},
            {"gif.label", "GIF"},

            // SetDailyWeeklyPopup
            {"daily.title", "Set Featured Level"},
            {"daily.set_daily", "Set Daily"},
            {"daily.set_weekly", "Set Weekly"},
            {"daily.success_daily_title", "Success"},
            {"daily.success_daily_msg", "Level set as Daily!"},
            {"daily.error_daily_title", "Error"},
            {"daily.error_daily_msg", "Failed to set Daily level."},
            {"daily.success_weekly_title", "Success"},
            {"daily.success_weekly_msg", "Level set as Weekly!"},
            {"daily.error_weekly_title", "Error"},
            {"daily.error_weekly_msg", "Failed to set Weekly level."},

            // TaskReadyToast
            {"toast.task_ready", "Task Ready"},

            // ThumbnailViewPopup
            {"thumbview.title", "Thumbnail"},

            // VerificationQueuePopup
            {"queue.banned_btn", "Banned"},
            {"queue.level_id", "Level {}"},
            {"queue.claimed_by_you", "Claimed by you"},
            {"queue.claimed_by_user", "Claimed by {}"},
            {"queue.view_btn", "View"},
            {"queue.view_thumb", "View Thumbnail"},
            {"queue.claim_btn", "Claim"},
            {"queue.unclaim_btn", "Unclaim"},
            {"queue.accept_btn", "Accept"},
            {"queue.reject_btn", "Reject"},
            {"queue.verified_by", "Verified by {}"},
            {"queue.reported_by", "Reported by {}"},
            {"queue.reason", "Reason: {}"},
            {"queue.ignore_btn", "Ignore"},
            {"queue.delete_btn", "Delete"},
            {"queue.keep_btn", "Keep"},
            {"queue.claiming", "Claiming level..."},
            {"queue.claimed", "Level claimed"},
            {"queue.claim_error", "Error: {}"},

            // ButtonEditOverlay
            {"edit.accept", "Accept"},
            {"edit.reset", "Reset"},
            {"edit.scale", "Scale:"},
            {"edit.opacity", "Opacity:"},

            // LeaderboardsLayer
            {"leaderboard.daily", "Daily"},
            {"leaderboard.weekly", "Weekly"},
            {"leaderboard.all_time", "All Time"},
            {"leaderboard.creators", "Creators"},
            {"leaderboard.error", "Error"},
            {"leaderboard.read_error", "Failed to read response"},
            {"leaderboard.load_error", "Failed to load leaderboard: {}"},
            {"leaderboard.parse_error", "Failed to parse JSON"},
            {"leaderboard.server_error", "Server error"},
            {"leaderboard.invalid_format", "Invalid data format"},
            {"leaderboard.loading", "Loading..."},
            {"leaderboard.unknown", "Unknown"},
            {"mods.title", "Paimbnails Moderators"},
            {"shop.title", "Paimon Shop"},
            {"shop.buy_coins", "Buy 100"},
            {"shop.upload_banner", "Upload Banner"},
            {"shop.premium_banners", "Premium Banners"},
            {"shop.page", "Page {}/{}"},
            {"shop.store_title", "Paimon Store"},
            {"shop.tab_shop", "Shop"},
            {"shop.tab_tasks", "Tasks"},
            {"leaderboard.no_refreshes", "No more refreshes available today!"},
            {"leaderboard.task_complete", "Task completed!"},
            {"leaderboard.no_gamemanager", "Could not get GameManager"},
            {"leaderboard.empty_username", "Empty username"},
            {"leaderboard.no_image", "No image selected"},
            {"leaderboard.png_open_error", "Could not open PNG"},
            {"leaderboard.profile_saved_local", "Profile saved locally (server upload disabled)"},
            {"leaderboard.uploading_profile", "Uploading profile..."},
            {"leaderboard.profile_uploaded", "Profile uploaded"},
            {"leaderboard.profile_error", "Failed to upload profile"},
            {"leaderboard.unknown_error", "Unknown error"},
            {"leaderboard.synced", "Synced with server!"},

            // GIFRecordSettingsPopup
            {"gif.start", "Start"},

            // PaimonShopLayer
            {"shop.admin_required", "Admin privileges required"},
            {"shop.file_open_error", "Could not open file"},
            {"shop.uploading_banner", "Uploading banner..."},
            {"shop.banner_uploaded", "uploaded successfully"},
            {"shop.banner_error", "Could not upload banner"},

            // PaimonCoinManager
            {"coins.already_owned", "You already own this banner!"},
            {"coins.purchased", "Banner purchased successfully!"},
            {"coins.not_enough", "Not enough Paimon Coins!"},

            // General
            {"general.error", "Error"},
            {"general.ok", "OK"},
            {"general.close", "Close"},

            // Missing keys added
            {"level.no_thumbnail_text", "No thumbnail"},
            {"level.saving_mod_folder", "Saving to mod folder..."},
            {"level.error_prefix", "Error: "},
            {"level.delete_moderator_only", "Only moderators can delete thumbnails"},
            {"level.deleting_server", "Deleting thumbnail from server..."},
            {"level.deleted_server", "Thumbnail deleted from server"},
            {"level.delete_error", "Error deleting: "},
            {"level.accepting", "Accepting thumbnail..."},
            {"level.accepted", "Thumbnail accepted!"},
            {"level.accept_error", "Error accepting: "},
            {"level.no_local_thumb", "No local thumbnail"},
            {"level.png_error", "Error generating PNG"},
            {"level.saved_local_server_disabled", "Thumbnail saved locally (server disabled)"},

            // Report Popup
            {"report.title", "Report Thumbnail"},
            {"report.cancel", "Cancel"},
            {"report.send", "Send"},
            {"report.placeholder", "Write here..."},
            {"report.empty_reason", "You must specify a reason"},
            {"report.sent_synced", "Report sent and synced: "},
            {"report.saved_local", "Report saved locally (offline)"},

            // PauseLayer
            {"pause.gif_not_supported", "GIF files not supported on this platform"},
            {"pause.process_image_error", "Error: Could not process image"},
            {"pause.create_texture_error", "Error: Could not create texture"},
            {"pause.init_texture_error", "Error: Could not initialize texture"},
            {"pause.username_error", "Error: Could not get username"},
            {"pause.gif_recording_started", "GIF recording started"},

            // Ban System
            {"ban.list.title", "Banned Users"},
            {"ban.list.loading", "Loading..."},
            {"ban.list.empty", "No bans"},
            {"ban.list.unban_btn", "Unban"},
            {"ban.info.no_info", "No information available."},
            {"ban.info.reason", "Reason"},
            {"ban.info.by", "By"},
            {"ban.info.date", "Date"},
            {"ban.info.title", "Ban Details"},
            {"ban.unban.title", "Unban User"},
            {"ban.unban.confirm", "Are you sure you want to unban <cy>{}</c>?"},
            {"ban.unban.success", "User unbanned"},
            {"ban.unban.error", "Error unbanning"},
            {"ban.popup.title", "Ban User"},
            {"ban.popup.user", "User: {}"},
            {"ban.popup.placeholder", "Ban reason..."},
            {"ban.popup.ban_btn", "Ban"},
            {"ban.popup.enter_reason", "Enter a reason"},
            {"ban.popup.success", "User banned"},
            {"ban.popup.error", "Error banning"},
            {"ban.profile.mod_only", "Moderators/Admins only"},
            {"ban.profile.self_ban", "You cannot ban yourself"},
            {"ban.profile.read_error", "Could not read username"}
        };
    }

    Language m_currentLanguage = Language::SPANISH;
    std::unordered_map<std::string, std::string> m_spanish;
    std::unordered_map<std::string, std::string> m_english;
};

