/**
 * @file ConfigStore.h
 * @brief Gestion centralisée des fichiers de configuration JSON.
 *
 * La classe ConfigStore charge au démarrage les différentes sections
 * de configuration depuis des fichiers JSON dans le répertoire
 * `/configuration`.  Chaque section est stockée dans un
 * DynamicJsonDocument et peut être modifiée en mémoire.  La
 * sauvegarde est différée et consolidée afin de minimiser les
 * écritures sur la flash.  Les modifications marquées sont écrites
 * dans un fichier temporaire puis renommées de façon atomique.
 */

#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <map>

/**
 * @class ConfigStore
 * Classe statique gérant le chargement et la sauvegarde des
 * différentes configurations du projet.  Les zones de configuration
 * connues sont : general, network, io, dmm, scope, funcgen, math.
 */
class ConfigStore {
public:
  /**
   * Charge toutes les zones de configuration depuis la flash.  Si un
   * fichier est absent ou corrompu, un document JSON vierge avec des
   * valeurs par défaut est créé.  Cette méthode doit être appelée
   * avant toute utilisation du réseau ou du serveur web.
   */
  static void begin();

  /**
   * Retourne la référence au document JSON d'une zone donnée.  Les
   * modifications apportées au document doivent être suivies d'un
   * appel à requestSave() pour planifier la sauvegarde.
   *
   * @param area Nom de la zone (general, network, io, dmm, scope,
   *             funcgen, math).
   * @return Référence au DynamicJsonDocument associé.
   */
  static DynamicJsonDocument& doc(const String& area);

  /**
   * Marque une zone comme modifiée et planifie sa sauvegarde
   * différée.  Les sauvegardes sont regroupées pour éviter les
   * écritures répétées.
   * @param area Nom de la zone à sauvegarder.
   */
  static void requestSave(const String& area);

  /**
   * Méthode à appeler régulièrement dans la boucle principale.  Elle
   * vérifie les zones modifiées et déclenche la sauvegarde après un
   * délai de débounce et un intervalle minimum entre deux écritures.
   */
  static void loop();

private:
  struct AreaState {
    String filename;
    DynamicJsonDocument *document;
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
