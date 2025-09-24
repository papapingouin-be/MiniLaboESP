/**
 * @file ConfigStore.h
 * @brief Gestion centralisÃ©e des fichiers de configuration JSON.
 *
 * La classe ConfigStore charge au dÃ©marrage les diffÃ©rentes sections
 * de configuration depuis des fichiers JSON dans le rÃ©pertoire
 * `/configuration`.  Chaque section est stockÃ©e dans un
 * JsonDocument et peut Ãªtre modifiÃ©e en mÃ©moire.  La
 * sauvegarde est diffÃ©rÃ©e et consolidÃ©e afin de minimiser les
 * Ã©critures sur la flash.  Les modifications marquÃ©es sont Ã©crites
 * dans un fichier temporaire puis renommÃ©es de faÃ§on atomique.
 */

#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <map>

/**
 * @class ConfigStore
 * Classe statique gÃ©rant le chargement et la sauvegarde des
 * diffÃ©rentes configurations du projet.  Les zones de configuration
 * connues sont : general, network, io, dmm, scope, funcgen, math.
 */
class ConfigStore {
public:
  /**
   * Charge toutes les zones de configuration depuis la flash.  Si un
   * fichier est absent ou corrompu, un document JSON vierge avec des
   * valeurs par dÃ©faut est crÃ©Ã©.  Cette mÃ©thode doit Ãªtre appelÃ©e
   * avant toute utilisation du rÃ©seau ou du serveur web.
   */
  static void begin();

  /**
   * Retourne la rÃ©fÃ©rence au document JSON d'une zone donnÃ©e.  Les
   * modifications apportÃ©es au document doivent Ãªtre suivies d'un
   * appel Ã  requestSave() pour planifier la sauvegarde.
   *
   * @param area Nom de la zone (general, network, io, dmm, scope,
   *             funcgen, math).
   * @return RÃ©fÃ©rence au JsonDocument associÃ©.
   */
  static JsonDocument& doc(const String& area);

  /**
   * Marque une zone comme modifiÃ©e et planifie sa sauvegarde
   * diffÃ©rÃ©e.  Les sauvegardes sont regroupÃ©es pour Ã©viter les
   * Ã©critures rÃ©pÃ©tÃ©es.
   * @param area Nom de la zone Ã  sauvegarder.
   */
  static void requestSave(const String& area);

  /**
   * MÃ©thode Ã  appeler rÃ©guliÃ¨rement dans la boucle principale.  Elle
   * vÃ©rifie les zones modifiÃ©es et dÃ©clenche la sauvegarde aprÃ¨s un
   * dÃ©lai de dÃ©bounce et un intervalle minimum entre deux Ã©critures.
   */
  static void loop();

private:
  struct AreaState {
    String filename;
    JsonDocument *document;
    bool dirty;
    unsigned long lastChange;
  };
  static std::map<String, AreaState> _areas;
  static unsigned long _lastSave;
  static const unsigned long DEBOUNCE_MS;
  static const unsigned long MIN_PERIOD_MS;
  static void loadArea(const String& area);
  static bool saveArea(const String& area);
};
