/**
 * @file IORegistry.h
 * @brief Registre centralisant toutes les entrées/sorties logiques.
 *
 * L'IORegistry permet de référencer des objets représentant les
 * sources ou sorties physiques du système (ADC interne, ADS1115,
 * DAC MCP4725, module PWM→0–10 V, etc.).  Chaque IO dérive de
 * IOBase et offre une méthode pour lire une valeur brute et/ou
 * écrire un pourcentage de sa pleine échelle.  L'initialisation se
 * fait à partir de la configuration io.json.
 */

#pragma once

#include <Arduino.h>
#include <vector>
#include <map>

class IOBase {
public:
  IOBase(const String &id) : _id(id) {}
  virtual ~IOBase() {}
  /** Lit la valeur brute (sans conversion en unité physique). */
  virtual float readRaw() { return 0.0f; }
  /** Écrit un pourcentage (0–100%) sur la sortie (si applicable). */
  virtual void writePercent(float percent) { (void)percent; }
  /** Retourne l'identifiant unique. */
  String id() const { return _id; }
  /**
   * Retourne la tension de référence associée à cette IO.  Par défaut
   * 1.0 pour signifier qu'il n'y a pas de conversion interne.  Les
   * classes dérivées peuvent surcharger cette méthode pour fournir
   * leur propre référence (ex. 1.0 V pour ADC interne, 4.096 V pour
   * ADS1115, etc.).
   */
  virtual float getVref() const { return 1.0f; }
  /**
   * Retourne le ratio diviseur appliqué entre la tension lue et la
   * tension réelle.  Par défaut 1.0.  Les classes dérivées comme
   * IO_A0 renvoient le ratio du pont diviseur de la carte.
   */
  virtual float getRatio() const { return 1.0f; }
protected:
  String _id;
};

/**
 * Implémentation pour l'ADC interne A0.  Cette classe lit la valeur
 * brute sur la broche A0.  La conversion en tension ou autres
 * grandeurs se fait au niveau des appareils (multimètre, scope).
 */
class IO_A0 : public IOBase {
public:
  IO_A0(const String &id, int bits, float vref, float ratio) :
    IOBase(id), _bits(bits), _vref(vref), _ratio(ratio) {}
  float readRaw() override {
    int code = analogRead(A0);
    // Normalisé 0..1 avant conversion; la conversion exacte sera
    // appliquée par la formule du multimètre ou du scope.
    return static_cast<float>(code) / ((1 << _bits) - 1);
  }
  float getVref() const override { return _vref; }
  float getRatio() const override { return _ratio; }
private:
  int _bits;
  float _vref;
  float _ratio;
};

/**
 * Classe pour un canal ADC ADS1115.  Cette implémentation est un
 * squelette : l'intégration réelle du pilote I2C doit être faite
 * dans la méthode readRaw().  Sans pilote, cette méthode renvoie
 * simplement zéro.
 */
class IO_ADS1115 : public IOBase {
public:
  IO_ADS1115(const String &id, uint8_t address, uint8_t channel, float pga) :
    IOBase(id), _address(address), _channel(channel), _pga(pga) {}
  float readRaw() override;
private:
  uint8_t _address;
  uint8_t _channel;
  float _pga;
};

/**
 * Classe pour une sortie analogique via MCP4725.  Ici on définit
 * simplement une fonction d'écriture de pourcentage, le pilote I2C
 * devant être ajouté dans writePercent().
 */
class IO_MCP4725 : public IOBase {
public:
  IO_MCP4725(const String &id, uint8_t address, int bits, float vref) :
    IOBase(id), _address(address), _bits(bits), _vref(vref) {}
  void writePercent(float percent) override;
private:
  uint8_t _address;
  int _bits;
  float _vref;
};

/**
 * Classe pour une sortie 0–10 V via module PWM→tension.  Le
 * pourcentage est converti en tension par la logique du module.  La
 * génération PWM doit être réalisée via analogWrite() ou un DAC.
 */
class IO_0_10V : public IOBase {
public:
  IO_0_10V(const String &id) : IOBase(id) {}
  void writePercent(float percent) override;
};

/**
 * Le registre central de toutes les IO logiques.
 */
class IORegistry {
public:
  /** Initialise le registre en créant les IO définies dans la config. */
  static void begin();
  /** Boucle d'entretien (actuellement vide). */
  static void loop() {}
  /** Retourne un pointeur vers une IO par son identifiant. */
  static IOBase* get(const String &id);
  /** Liste tous les IO sous forme d'un vecteur d'identifiants. */
  static std::vector<IOBase*> list();
private:
  static std::vector<IOBase*> _list;
  static std::map<String, IOBase*> _map;
  static void registerIO(IOBase* io);
};
