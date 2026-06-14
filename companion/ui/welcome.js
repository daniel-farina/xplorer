/** First-run welcome screen for Xplorer. */

const getStartedBtn = document.getElementById('get-started');

async function completeWelcome() {
  if (getStartedBtn) {
    getStartedBtn.disabled = true;
    getStartedBtn.textContent = 'Starting…';
  }
  try {
    await saveSettings({ welcome_completed: true });
  } catch (err) {
    console.error('welcome complete failed', err);
    if (getStartedBtn) {
      getStartedBtn.disabled = false;
      getStartedBtn.textContent = 'Get started';
    }
    return;
  }
  try {
    const settings = await fetchSettings();
    const home = settings.search_home || SEARCH_HOME_BUILD;
    if (home === SEARCH_HOME_WEB) {
      window.location.href = settings.grok_web_url || 'https://grok.com/';
      return;
    }
    if (home === SEARCH_HOME_WIKI) {
      window.location.href = settings.grok_wiki_url || 'https://grokipedia.com/';
      return;
    }
    window.location.href = settings.grok_build_url || '/search';
  } catch {
    window.location.href = '/search';
  }
}

getStartedBtn?.addEventListener('click', completeWelcome);