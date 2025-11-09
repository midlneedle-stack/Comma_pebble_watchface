(() => {
  const TAG = 'general_magic-js';
  const CONFIG_URL = 'https://midlneedle-stack.github.io/General_Magic_pebble_watchface/config/index.html';
  const SETTINGS_KEY = 'general_magic_settings';
  const DEFAULT_SETTINGS = {
    timeFormat: '24',
    theme: 'dark',
    vibration: true,
    animation: true,
    vibrateOnOpen: true,
    hourlyChime: false,
  };

  const loadSettings = () => {
    try {
      const raw = localStorage.getItem(SETTINGS_KEY);
      if (raw) {
        return Object.assign({}, DEFAULT_SETTINGS, JSON.parse(raw));
      }
    } catch (err) {
      console.warn(`${TAG}: failed to parse settings`, err);
    }
    return Object.assign({}, DEFAULT_SETTINGS);
  };

  let settings = loadSettings();

  const persistSettings = () => {
    try {
      localStorage.setItem(SETTINGS_KEY, JSON.stringify(settings));
    } catch (err) {
      console.warn(`${TAG}: failed to persist settings`, err);
    }
  };

  const sendSettingsToWatch = () => {
    Pebble.sendAppMessage(
      {
        TimeFormat: settings.timeFormat === '24' ? 24 : 12,
        Theme: settings.theme === 'light' ? 1 : 0,
        Vibration: settings.vibration ? 1 : 0,
        Animation: settings.animation ? 1 : 0,
        VibrateOnOpen: settings.vibrateOnOpen ? 1 : 0,
        HourlyChime: settings.hourlyChime ? 1 : 0,
      },
      () => console.log(`${TAG}: settings sent`),
      (err) => console.warn(`${TAG}: failed to send settings`, err)
    );
  };

  const buildConfigUrl = () => {
    const state = encodeURIComponent(JSON.stringify(settings));
    return `${CONFIG_URL}?state=${state}`;
  };

  Pebble.addEventListener('ready', () => {
    console.log(`${TAG}: ready`);
    Pebble.sendAppMessage({ SettingsRequest: 1 }, null, (err) =>
      console.warn(`${TAG}: settings request failed`, err)
    );
  });

  Pebble.addEventListener('appmessage', (event) => {
    const payload = event.payload || {};
    let changed = false;
    if (typeof payload.TimeFormat !== 'undefined') {
      const value = payload.TimeFormat === 24 ? '24' : '12';
      if (settings.timeFormat !== value) {
        settings.timeFormat = value;
        changed = true;
      }
    }
    if (typeof payload.Theme !== 'undefined') {
      const value = payload.Theme === 1 ? 'light' : 'dark';
      if (settings.theme !== value) {
        settings.theme = value;
        changed = true;
      }
    }
    ['Vibration', 'Animation', 'VibrateOnOpen', 'HourlyChime'].forEach((key) => {
      if (typeof payload[key] !== 'undefined') {
        const field = key.charAt(0).toLowerCase() + key.slice(1);
        const boolValue = payload[key] === 1;
        if (settings[field] !== boolValue) {
          settings[field] = boolValue;
          changed = true;
        }
      }
    });
    if (changed) {
      persistSettings();
    }
  });

  Pebble.addEventListener('showConfiguration', () => {
    Pebble.openURL(buildConfigUrl());
  });

  Pebble.addEventListener('webviewclosed', (event) => {
    if (!event || !event.response) {
      return;
    }
    try {
      const response = JSON.parse(decodeURIComponent(event.response));
      settings = Object.assign({}, settings, response);
      persistSettings();
      sendSettingsToWatch();
    } catch (err) {
      console.warn(`${TAG}: failed to parse config`, err);
    }
  });
})();
