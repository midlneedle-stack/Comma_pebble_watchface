(() => {
  const TAG = 'general_magic-js';
  const CONFIG_URL = 'https://midlneedle-stack.github.io/General_Magic_pebble_watchface/config/index.html';
  const SETTINGS_KEY = 'general_magic_settings';
  const HOURLY_CHIME_STRENGTHS = ['light', 'medium', 'hard'];
  const normalizeHourlyStrength = (value) => {
    return HOURLY_CHIME_STRENGTHS.indexOf(value) === -1 ? 'medium' : value;
  };
  const strengthToIndex = (value) => {
    const normalized = normalizeHourlyStrength(value);
    const idx = HOURLY_CHIME_STRENGTHS.indexOf(normalized);
    return idx === -1 ? 1 : idx;
  };
  const indexToStrength = (value) => {
    const idx = typeof value === 'number' ? value : parseInt(value, 10);
    return HOURLY_CHIME_STRENGTHS[idx] || 'medium';
  };

  const DEFAULT_SETTINGS = {
    timeFormat: '24',
    theme: 'dark',
    vibration: true,
    animation: true,
    vibrateOnOpen: true,
    hourlyChime: false,
    hourlyChimeStrength: 'medium',
  };

  const loadSettings = () => {
    try {
      const raw = localStorage.getItem(SETTINGS_KEY);
      if (raw) {
        const parsed = JSON.parse(raw);
        const merged = Object.assign({}, DEFAULT_SETTINGS, parsed);
        merged.hourlyChimeStrength = normalizeHourlyStrength(merged.hourlyChimeStrength);
        return merged;
      }
    } catch (err) {
      console.warn(`${TAG}: failed to parse settings`, err);
    }
    return Object.assign({}, DEFAULT_SETTINGS);
  };

  let settings = loadSettings();
  settings.hourlyChimeStrength = normalizeHourlyStrength(settings.hourlyChimeStrength);

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
        HourlyChimeStrength: strengthToIndex(settings.hourlyChimeStrength),
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
    if (typeof payload.HourlyChimeStrength !== 'undefined') {
      const newStrength = indexToStrength(payload.HourlyChimeStrength);
      if (settings.hourlyChimeStrength !== newStrength) {
        settings.hourlyChimeStrength = newStrength;
        changed = true;
      }
    }
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
      settings.hourlyChimeStrength = normalizeHourlyStrength(settings.hourlyChimeStrength);
      persistSettings();
      sendSettingsToWatch();
    } catch (err) {
      console.warn(`${TAG}: failed to parse config`, err);
    }
  });
})();
