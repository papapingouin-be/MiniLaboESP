/**
 * @file UDPServer.h
 * @brief Serveur UDP pour diffusion et réception de données MiniLabo.
 *
 * Ce module encapsule un serveur UDP utilisant WiFiUDP.  Il permet
 * d'envoyer périodiquement des informations (par exemple les
 * mesures du multimètre) vers un destinataire (broadcast ou
 * unicast) et de recevoir des messages entrants.  Les paramètres
 * réseau (port d'écoute, port de destination, adresse de
 * destination et activation) sont chargés depuis la configuration
 * `network.json` (champs optionnels `udp_port`, `udp_dest` et
 * `udp_enabled`).
 */

#pragma once

#include <Arduino.h>
#include <WiFiUdp.h>

namespace UDPServer {

  /** Initialise le serveur UDP en fonction de la configuration.
   *  Ce doit être appelé après la configuration réseau (WiFi) et
   *  l'initialisation de ConfigStore.  En cas d'échec d'ouverture,
   *  une ligne de log est émise mais l'application continue.
   */
  bool begin();

  /** Boucle de traitement périodique.  Gère la réception de
   *  messages entrants et émet des données toutes les `emit_interval` ms.
   *  Doit être appelé à chaque itération de loop().
   */
  void loop();

  /** Active ou désactive l'émission de données.  Si `enable` est
   *  false, aucune donnée n'est envoyée mais la réception reste
   *  active. */
  void setEmitEnabled(bool enable);

  /** Indique si la fonctionnalité UDP est activée dans la configuration. */
  bool isEnabled();

  /** Retourne le port local utilisé pour l'écoute UDP. */
  uint16_t port();

} // namespace UDPServer
