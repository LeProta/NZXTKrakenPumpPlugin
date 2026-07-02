#pragma once
#include <QString>

namespace NZXTKrakenPump {

// ─────────────────────────────────────────────────────────────────────────────
//  Lecture (lecture seule) des réglages d'OpenRGB depuis OpenRGB.json,
//  section "UserInterface". Permet au plugin de suivre les choix d'OpenRGB
//  (langue/locale) sans lier OpenRGB.exe.
//
//  Les valeurs sont relues à la demande à chaque appel : comme OpenRGB réécrit
//  OpenRGB.json dès qu'un réglage change, lire au moment de l'utilisation suffit
//  à suivre les changements sans redémarrage (ex. sur QEvent::LanguageChange
//  pour l'unité de température).
// ─────────────────────────────────────────────────────────────────────────────
namespace OpenRGBSettings {

// Dossier contenant OpenRGB.json (et le sous-dossier logs/).
QString configDir();

// Langue OpenRGB, ex. "English - US" ou "default" (défaut : "default").
QString language();

// true si la langue/locale OpenRGB correspond à une région utilisant le
// Fahrenheit (principalement "English - US" ou locale système US).
bool prefersFahrenheit();

} // namespace OpenRGBSettings
} // namespace NZXTKrakenPump
