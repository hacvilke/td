// =============================================================================
// i18n — Multi-locale text for the showcase.
// -----------------------------------------------------------------------------
// Wraps TDEngine.i18n.* (which calls into the C++ side td_i18n_load /
// td_i18n_t) when WASM is loaded, and falls back to a pure-JS implementation
// when it isn't.  Three locales are bundled inline to demonstrate the
// engine's runtime locale-swap capability.
//
// Usage:
//   TDSandbox.i18n.setLocale('es');
//   const t = TDSandbox.i18n.t('hint.move');  // -> "Mover"
//
// Mirrors the TDEngine.i18n API shape so the showcase serves as a reference
// for how game code would call the engine in production.
// =============================================================================

(function (global) {
  'use strict';

  const STRINGS = {
    en: {
      'hint.move':     'Move',
      'hint.jump':     'Jump',
      'hint.look':     'Look',
      'hint.shoot':    'Shoot',
      'hint.reset':    'Reset',
      'hint.spawn':    'Spawn',
      'hint.beat':     'Toggle beat',
      'hint.godmode':  'God mode',
      'spawn.title':   'Spawn',
      'spawn.box':     'Box',
      'spawn.sphere':  'Sphere',
      'spawn.capsule': 'Capsule',
      'spawn.stack':   'Stack',
      'spawn.pendulum':'Pendulum',
      'spawn.energy':  'Energy ball',
      'toast.saved':   'Scene saved',
      'toast.loaded':  'Scene loaded',
      'toast.reset':   'Scene reset',
      'toast.beatOn':  'Beat sync ON',
      'toast.beatOff': 'Beat sync OFF',
      'toast.godOn':   'God mode ON',
      'toast.godOff':  'God mode OFF',
    },
    es: {
      'hint.move':     'Mover',
      'hint.jump':     'Saltar',
      'hint.look':     'Mirar',
      'hint.shoot':    'Disparar',
      'hint.reset':    'Reiniciar',
      'hint.spawn':    'Crear',
      'hint.beat':     'Ritmo on/off',
      'hint.godmode':  'Modo dios',
      'spawn.title':   'Crear',
      'spawn.box':     'Caja',
      'spawn.sphere':  'Esfera',
      'spawn.capsule': 'Cápsula',
      'spawn.stack':   'Pila',
      'spawn.pendulum':'Péndulo',
      'spawn.energy':  'Bola energía',
      'toast.saved':   'Escena guardada',
      'toast.loaded':  'Escena cargada',
      'toast.reset':   'Escena reiniciada',
      'toast.beatOn':  'Ritmo ON',
      'toast.beatOff': 'Ritmo OFF',
      'toast.godOn':   'Modo dios ON',
      'toast.godOff':  'Modo dios OFF',
    },
    fr: {
      'hint.move':     'Déplacer',
      'hint.jump':     'Sauter',
      'hint.look':     'Regarder',
      'hint.shoot':    'Tirer',
      'hint.reset':    'Réinitialiser',
      'hint.spawn':    'Créer',
      'hint.beat':     'Rythme on/off',
      'hint.godmode':  'Mode dieu',
      'spawn.title':   'Créer',
      'spawn.box':     'Boîte',
      'spawn.sphere':  'Sphère',
      'spawn.capsule': 'Capsule',
      'spawn.stack':   'Pile',
      'spawn.pendulum':'Pendule',
      'spawn.energy':  'Boule énergie',
      'toast.saved':   'Scène sauvegardée',
      'toast.loaded':  'Scène chargée',
      'toast.reset':   'Scène réinitialisée',
      'toast.beatOn':  'Rythme ON',
      'toast.beatOff': 'Rythme OFF',
      'toast.godOn':   'Mode dieu ON',
      'toast.godOff':  'Mode dieu OFF',
    },
  };

  let _locale = 'en';

  function setLocale(loc) {
    if (!STRINGS[loc]) loc = 'en';
    _locale = loc;
    // If the WASM engine is loaded, push the locale into the C++ side too
    // so tdscript runtime / engine logs come out in the right language.
    if (global.TDEngine && global.TDEngine.i18n && global.TDEngine.i18n.setLocale) {
      try { global.TDEngine.i18n.setLocale(loc); } catch (e) { /* noop */ }
    }
    // Re-render all [data-i18n] elements.
    document.querySelectorAll('[data-i18n]').forEach(function (el) {
      const key = el.getAttribute('data-i18n');
      el.textContent = t(key);
    });
    // Update the locale badge.
    const badge = document.getElementById('hud-locale');
    if (badge) badge.textContent = loc.toUpperCase();
  }

  function t(key) {
    const table = STRINGS[_locale] || STRINGS.en;
    return table[key] || STRINGS.en[key] || key;
  }

  function getLocale() { return _locale; }
  function listLocales() { return Object.keys(STRINGS); }

  global.TDSandbox = global.TDSandbox || {};
  global.TDSandbox.i18n = { setLocale, t, getLocale, listLocales };
})(typeof window !== 'undefined' ? window : this);
