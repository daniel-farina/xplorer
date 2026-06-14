// Copyright 2026 The Aether Authors.
// Use of this source code is governed by a BSD-style license.

#include "chrome/browser/grok_companion/grok_fab.h"

#include <set>
#include <string>

#include "base/functional/bind.h"
#include "base/json/json_writer.h"
#include "base/logging.h"
#include "base/strings/string_util.h"
#include "base/strings/stringprintf.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/agent_gateway/grok_native.h"
#include "content/public/common/isolated_world_ids.h"
#include "base/task/sequenced_task_runner.h"
#include "base/time/time.h"
#include "base/values.h"
#include "chrome/browser/agent_gateway/agent_gateway.h"
#include "chrome/browser/grok_companion/grok_companion_util.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/common/chrome_isolated_world_ids.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_observer.h"
#include "content/public/browser/web_contents_user_data.h"
#include "url/gurl.h"

namespace grok_companion {
namespace {

constexpr int kMaxInjectAttempts = 40;

int GatewayPort() {
  if (auto* gw = agent_gateway::AgentGateway::GetInstance())
    return gw->port();
  return kCompanionPort;
}

bool IsFabHost(const GURL& url) {
  if (!url.is_valid() || !url.SchemeIsHTTPOrHTTPS())
    return false;
  if (url.host() == kCompanionHost &&
      url.EffectiveIntPort() == GatewayPort()) {
    return false;
  }
  const std::string_view host = url.host();
  if (host == "grok.com" || host == "www.grok.com" ||
      base::EndsWith(host, ".grok.com") || host == "grokipedia.com" ||
      host == "www.grokipedia.com" ||
      base::EndsWith(host, ".grokipedia.com")) {
    return false;
  }
  return true;
}

std::string JsonStringLiteral(const std::string& value) {
  std::string json;
  base::JSONWriter::Write(base::Value(value), &json);
  return json;
}

std::string ExtractGrokWebPendingId(const GURL& url) {
  if (!url.is_valid())
    return {};
  constexpr char kKey[] = "xbrowser_grok=";
  const std::string query(url.query());
  size_t pos = query.find(kKey);
  if (pos != std::string::npos) {
    std::string id = query.substr(pos + sizeof(kKey) - 1);
    const size_t amp = id.find('&');
    if (amp != std::string::npos)
      id = id.substr(0, amp);
    return id;
  }
  const std::string ref(url.ref());
  pos = ref.find(kKey);
  if (pos == std::string::npos)
    return {};
  std::string id = ref.substr(pos + sizeof(kKey) - 1);
  const size_t amp = id.find('&');
  if (amp != std::string::npos)
    id = id.substr(0, amp);
  return id;
}

std::string BuildGrokWebNativeSubmitScript(const std::string& prompt,
                                           const std::string& id) {
  return base::StringPrintf(
      R"((function(){
  var p=%s;
  var id=%s;
  console.log('[xbrowser] native inject grok-web id='+id+' prompt_len='+p.length);
  if(window.__xbrowserNativeGrokSubmit){
    window.__xbrowserNativeGrokSubmit(p,id);
  }else{
    window.__xbrowserGrokWebNativeQueue=window.__xbrowserGrokWebNativeQueue||[];
    window.__xbrowserGrokWebNativeQueue.push({prompt:p,id:id});
    console.log('[xbrowser] queued native grok-web (fab script not ready)');
  }
})();)",
      JsonStringLiteral(prompt).c_str(), JsonStringLiteral(id).c_str());
}

std::string BuildFabInjectScript() {
  const std::string gw =
      base::StringPrintf("http://%s:%d", kCompanionHost, GatewayPort());
  const std::string gw_json = JsonStringLiteral(gw);

  return base::StringPrintf(
      R"((function(){
  if(!document.documentElement)return;
  var GW=%s;
  var FAB_ID='xbrowser-grok-fab',STYLE_ID='xbrowser-grok-fab-style';
  var DOC_ICON='<span class="xfab-doc" aria-hidden="true">'
    +'<span class="xfab-doc-fold"></span>'
    +'<span class="xfab-doc-art">'
    +'<svg viewBox="0 0 36 24" class="xfab-landscape"><rect width="36" height="24" rx="4" fill="#b0b0b0"/>'
    +'<circle cx="26" cy="7" r="3" fill="#9a9a9a"/>'
    +'<path d="M3 20 L13 12 L19 16 L27 8 L36 20 Z" fill="#9a9a9a"/></svg>'
    +'</span><span class="xfab-doc-label">Grok</span></span>';
  var css='#xbrowser-grok-fab{position:fixed;bottom:22px;right:22px;z-index:2147483646;border:none;background:transparent;padding:0;cursor:pointer;-webkit-font-smoothing:antialiased;transition:transform .15s,filter .15s}'
    +'#xbrowser-grok-fab:hover{transform:translateY(-2px);filter:brightness(1.03)}'
    +'#xbrowser-grok-fab:disabled{opacity:.6;cursor:wait;transform:none}'
    +'.xfab-doc{position:relative;display:flex;flex-direction:column;align-items:center;width:54px;padding:10px 8px 8px;background:#f8f8f8;border-radius:12px;box-shadow:0 4px 16px rgba(0,0,0,.22),0 1px 3px rgba(0,0,0,.12)}'
    +'.xfab-doc-fold{position:absolute;top:0;right:0;width:0;height:0;border-style:solid;border-width:0 18px 18px 0;border-color:transparent #e4e4e4 transparent transparent;border-top-right-radius:12px;filter:drop-shadow(-1px 1px 1px rgba(0,0,0,.08))}'
    +'.xfab-doc-fold::after{content:"";position:absolute;top:0;right:-18px;width:0;height:0;border-style:solid;border-width:0 17px 17px 0;border-color:transparent #fff transparent transparent}'
    +'.xfab-doc-art{display:flex;align-items:center;justify-content:center;width:38px;height:28px;margin-top:2px;border-radius:5px;overflow:hidden;background:#b8b8b8}'
    +'.xfab-landscape{display:block;width:38px;height:28px}'
    +'.xfab-doc-label{margin-top:6px;font:500 11px/1 -apple-system,BlinkMacSystemFont,sans-serif;letter-spacing:.14em;color:#a0a0a0;text-transform:uppercase}'
    +'#xbrowser-grok-fab.busy .xfab-doc-label::after{content:"…"}';
  var state=window.__xbrowserGrokFabState||(window.__xbrowserGrokFabState={busy:false,pageData:null});
  function xlog(){
    try{console.log.apply(console,['[xbrowser-fab]'].concat(Array.prototype.slice.call(arguments)));}catch(e){}
  }
  function extractPage(){
    var kill='script,style,noscript,svg,nav,footer,aside,iframe,#xbrowser-grok-bar,#xbrowser-grok-fab,[data-aether-hud]';
    var selText=(window.getSelection&&window.getSelection().toString()||'').replace(/\s+/g,' ').trim();
    if(selText.length>80){
      xlog('extract: user selection',selText.length,'chars');
      if(selText.length>50000)selText=selText.slice(0,50000)+'\n\n[truncated]';
      return {title:document.title||'',url:location.href,text:selText,source:'selection'};
    }
    var text='';
    try{
      var root=document.querySelector('main,article,[role=main]')||document.body;
      if(root&&window.getSelection&&document.createRange){
        var range=document.createRange();
        range.selectNodeContents(root);
        var sel=window.getSelection();
        sel.removeAllRanges();
        sel.addRange(range);
        text=(sel.toString()||'').replace(/\n{3,}/g,'\n\n').trim();
        sel.removeAllRanges();
      }
    }catch(e){xlog('select-all failed',e);}
    if(!text){
      var doc=document.cloneNode(true);
      doc.querySelectorAll(kill).forEach(function(n){n.remove();});
      var main=doc.querySelector('main,article,[role=main]')||doc.body;
      text=(main?main.innerText:'').replace(/\n{3,}/g,'\n\n').trim();
      xlog('extract: clone fallback',text.length,'chars');
    }else{
      xlog('extract: select-all',text.length,'chars');
    }
    if(text.length>50000)text=text.slice(0,50000)+'\n\n[truncated]';
    return {title:document.title||'',url:location.href,text:text,source:text?'select-all':'empty'};
  }
  function ensureFab(){
    if(!document.documentElement)return;
    var style=document.getElementById(STYLE_ID);
    if(!style){
      style=document.createElement('style');
      style.id=STYLE_ID;
      document.documentElement.appendChild(style);
    }
    style.textContent=css;
    var fab=document.getElementById(FAB_ID);
    if(!fab){
      fab=document.createElement('button');
      fab.id=FAB_ID;
      fab.type='button';
      document.documentElement.appendChild(fab);
    }else if(fab.parentNode!==document.documentElement){
      document.documentElement.appendChild(fab);
    }
    fab.title='Grok this page on Grok Web';
    fab.setAttribute('aria-label','Grok this page on Grok Web');
    fab.innerHTML=DOC_ICON;
    var oldPanel=document.getElementById('xbrowser-grok-panel');
    if(oldPanel)oldPanel.remove();
    if(fab.dataset.wired==='4')return;
    fab.dataset.wired='4';
    function setBusy(on){
      state.busy=on;
      fab.disabled=on;
      fab.classList.toggle('busy',on);
    }
    function runGrokWeb(){
      if(state.busy)return;
      state.pageData=extractPage();
      if(!state.pageData.text){
        alert('No readable text on this page.');
        return;
      }
      setBusy(true);
      xlog('grok web',state.pageData.source,state.pageData.text.length);
      fetch(GW+'/api/page/grok-web',{
        method:'POST',
        headers:{'Content-Type':'application/json'},
        body:JSON.stringify(state.pageData)
      }).then(function(r){return r.json();}).then(function(d){
        if(d.error)throw new Error(d.error);
        if(d.grok_url)window.open(d.grok_url,'_blank');
        else throw new Error('missing grok_url');
      }).catch(function(e){
        alert('Grok Web failed: '+(e.message||e));
      }).finally(function(){setBusy(false);});
    }
    fab.onclick=runGrokWeb;
  }
  function getGrokWebPendingId(){
    var id=new URLSearchParams(location.search).get('xbrowser_grok');
    if(id)return id;
    var m=(location.hash||'').match(/xbrowser_grok=([^&]+)/);
    return m?decodeURIComponent(m[1]):null;
  }
  function clearGrokWebPendingId(){
    try{
      var u=new URL(location.href);
      u.searchParams.delete('xbrowser_grok');
      u.hash=(u.hash||'').replace(/xbrowser_grok=[^&]+&?/,'').replace(/&$/,'');
      history.replaceState(null,'',u.pathname+u.search+u.hash);
    }catch(e){}
  }
  function submitGrokWebPrompt(prompt,onSuccess){
    xlog('submitGrokWebPrompt start, chars=',prompt.length);
    var submitted=false;
    function visible(el){
      if(!el)return false;
      var r=el.getBoundingClientRect();
      return r.width>0&&r.height>0;
    }
    function setValue(el,value){
      try{
        el.focus();
        if(el.isContentEditable||el.getAttribute('contenteditable')==='true'||
           el.getAttribute('role')==='textbox'){
          el.textContent='';
          var sel=window.getSelection();
          var range=document.createRange();
          range.selectNodeContents(el);
          range.collapse(true);
          sel.removeAllRanges();
          sel.addRange(range);
          if(document.execCommand('insertText',false,value)){
            el.dispatchEvent(new InputEvent('input',{bubbles:true,inputType:'insertText',data:value}));
            return true;
          }
          el.textContent=value;
          el.dispatchEvent(new InputEvent('beforeinput',{bubbles:true,cancelable:true,inputType:'insertText',data:value}));
          el.dispatchEvent(new InputEvent('input',{bubbles:true,inputType:'insertText',data:value}));
          return true;
        }
        if(el.tagName==='TEXTAREA'||el.tagName==='INPUT'){
          var proto=el.tagName==='TEXTAREA'?HTMLTextAreaElement.prototype:HTMLInputElement.prototype;
          var desc=Object.getOwnPropertyDescriptor(proto,'value');
          if(desc&&desc.set)desc.set.call(el,value);
          else el.value=value;
          el.dispatchEvent(new Event('input',{bubbles:true}));
          el.dispatchEvent(new Event('change',{bubbles:true}));
          return true;
        }
      }catch(e){}
      return false;
    }
    function findComposer(){
      var selectors=[
        'textarea[placeholder*="Ask" i]','textarea[placeholder*="Grok" i]',
        'textarea[aria-label*="Ask" i]','textarea[aria-label*="Message" i]',
        'div[contenteditable="true"][role="textbox"]','div[contenteditable="true"]',
        '[role="textbox"][contenteditable="true"]',
        'textarea:not([disabled]):not([aria-hidden="true"])',
        'textarea','[contenteditable="true"]'
      ];
      for(var i=0;i<selectors.length;i++){
        var nodes=document.querySelectorAll(selectors[i]);
        for(var j=nodes.length-1;j>=0;j--){
          if(visible(nodes[j]))return nodes[j];
        }
      }
      return null;
    }
    function findSendButton(){
      var selectors=[
        'button[aria-label*="Send" i]','button[aria-label*="Submit" i]',
        'button[data-testid*="send" i]','button[type="submit"]'
      ];
      for(var i=0;i<selectors.length;i++){
        var btn=document.querySelector(selectors[i]);
        if(btn&&!btn.disabled&&visible(btn))return btn;
      }
      var buttons=document.querySelectorAll('button');
      for(var k=buttons.length-1;k>=0;k--){
        var b=buttons[k];
        if(b.disabled||!visible(b))continue;
        var label=(b.getAttribute('aria-label')||b.textContent||'').toLowerCase();
        if(label.indexOf('send')>=0||label.indexOf('submit')>=0)return b;
      }
      return null;
    }
    function clickSend(el){
      var btn=findSendButton();
      if(btn&&!btn.disabled){btn.click();return true;}
      if(el){
        var ev={bubbles:true,cancelable:true,key:'Enter',code:'Enter',keyCode:13,which:13};
        el.dispatchEvent(new KeyboardEvent('keydown',ev));
        el.dispatchEvent(new KeyboardEvent('keypress',ev));
        el.dispatchEvent(new KeyboardEvent('keyup',ev));
      }
      return false;
    }
    var composerFilled=false;
    function tryOnce(){
      if(submitted)return true;
      var el=findComposer();
      if(!el){
        xlog('submit: composer not found');
        return false;
      }
      if(!composerFilled){
        var ok=setValue(el,prompt);
        composerFilled=true;
        xlog('submit: filled composer',ok,el.tagName,el.getAttribute&&el.getAttribute('role'));
      }
      var btn=findSendButton();
      if(btn&&!btn.disabled){
        xlog('submit: clicking send');
        btn.click();
        submitted=true;
        if(onSuccess)onSuccess();
        return true;
      }
      if(composerFilled){
        xlog('submit: trying enter key');
        clickSend(el);
      }
      return false;
    }
    var attempts=0;
    function tick(){
      if(submitted)return;
      tryOnce();
      if(submitted)return;
      if(++attempts>90)return;
      setTimeout(tick,400);
    }
    tick();
    if(!window.__xbrowserGrokSubmitWatch){
      window.__xbrowserGrokSubmitWatch=true;
      new MutationObserver(function(){if(!submitted)tryOnce();})
        .observe(document.documentElement,{childList:true,subtree:true});
    }
  }
  window.__xbrowserNativeGrokSubmit=function(prompt,id){
    xlog('nativeGrokSubmit id='+id+' len='+prompt.length);
    if(window.__xbrowserGrokConsuming===id||window.__xbrowserGrokWebSubmitDone===id)return;
    window.__xbrowserGrokConsuming=id;
    submitGrokWebPrompt(prompt,function(){
      xlog('grok-web submit done id='+id);
      window.__xbrowserGrokWebSubmitDone=id;
      fetch(GW+'/api/page/grok-web/consumed?id='+encodeURIComponent(id),{method:'POST'})
        .then(function(){xlog('consumed ok');})
        .catch(function(e){xlog('consumed fetch failed',e);});
      clearGrokWebPendingId();
      window.__xbrowserGrokConsuming='';
    });
  };
  if(window.__xbrowserGrokWebNativeQueue){
    window.__xbrowserGrokWebNativeQueue.forEach(function(q){
      window.__xbrowserNativeGrokSubmit(q.prompt,q.id);
    });
    window.__xbrowserGrokWebNativeQueue=[];
  }
  function tryConsumeGrokWebPending(){
    var host=location.hostname||'';
    if(host.indexOf('grok.com')<0&&host.indexOf('.grok.com')<0)return;
    var id=getGrokWebPendingId();
    if(!id)return;
    if(window.__xbrowserGrokConsuming===id||window.__xbrowserGrokWebSubmitDone===id)return;
    xlog('fetch fallback pending id='+id);
    window.__xbrowserGrokConsuming=id;
    fetch(GW+'/api/page/grok-web/pending?id='+encodeURIComponent(id))
      .then(function(r){return r.json();})
      .then(function(d){
        if(!d.prompt){
          xlog('fetch fallback: no prompt',d);
          window.__xbrowserGrokConsuming='';
          return;
        }
        submitGrokWebPrompt(d.prompt,function(){
          window.__xbrowserGrokWebSubmitDone=id;
          fetch(GW+'/api/page/grok-web/consumed?id='+encodeURIComponent(id),{method:'POST'});
          clearGrokWebPendingId();
          window.__xbrowserGrokConsuming='';
        });
      }).catch(function(e){
        xlog('fetch fallback failed',e);
        window.__xbrowserGrokConsuming='';
      });
  }
  function fabNeedsMount(){
    var fab=document.getElementById(FAB_ID);
    return !fab||fab.parentNode!==document.documentElement;
  }
  ensureFab();
  tryConsumeGrokWebPending();
  if((location.hostname||'').indexOf('grok.com')>=0&&!window.__xbrowserGrokConsumeWatch){
    window.__xbrowserGrokConsumeWatch=true;
    setInterval(tryConsumeGrokWebPending,1000);
  }
  if(!window.__xbrowserGrokFabWatch){
    window.__xbrowserGrokFabWatch=true;
    new MutationObserver(function(){
      if(fabNeedsMount()) ensureFab();
    }).observe(document.documentElement,{childList:true,subtree:true});
    setInterval(function(){
      if(fabNeedsMount()) ensureFab();
    },1500);
  }
})();)",
      gw_json.c_str());
}

class GrokFabInjector : public content::WebContentsObserver,
                        public content::WebContentsUserData<GrokFabInjector> {
 public:
  ~GrokFabInjector() override = default;

  static void Attach(content::WebContents* contents) {
    if (!contents)
      return;
    Profile* profile =
        Profile::FromBrowserContext(contents->GetBrowserContext());
    if (!profile || !profile->IsRegularProfile())
      return;
    if (!FromWebContents(contents))
      CreateForWebContents(contents);
  }

  void RefreshInjection() {
    RedirectLegacyNewTabIfNeeded(web_contents());
    ScheduleInject();
  }

  void DidFinishNavigation(
      content::NavigationHandle* navigation_handle) override {
    if (!navigation_handle->IsInPrimaryMainFrame() ||
        !navigation_handle->HasCommitted() ||
        navigation_handle->IsErrorPage()) {
      return;
    }
    const GURL& url = navigation_handle->GetURL();
    if (IsLegacyChromeNewTab(url)) {
      RedirectLegacyNewTabIfNeeded(web_contents());
      return;
    }
    if (!IsFabHost(url))
      return;
    ScheduleInject();
    if (!ExtractGrokWebPendingId(url).empty())
      ScheduleGrokWebSubmitBurst();
  }

  void DocumentOnLoadCompletedInPrimaryMainFrame() override {
    ScheduleInject();
    MaybeScheduleGrokWebBurst();
  }

  void DidStopLoading() override {
    ScheduleInject();
    MaybeScheduleGrokWebBurst();
  }

 private:
  friend class content::WebContentsUserData<GrokFabInjector>;

  explicit GrokFabInjector(content::WebContents* contents)
      : content::WebContentsObserver(contents),
        content::WebContentsUserData<GrokFabInjector>(*contents) {
    RedirectLegacyNewTabIfNeeded(contents);
    ScheduleInject();
    ScheduleStartupBurst();
  }

  bool ShouldInject(content::WebContents* contents) const {
    if (!contents)
      return false;
    if (IsFabHost(contents->GetLastCommittedURL()))
      return true;
    return IsFabHost(contents->GetVisibleURL());
  }

  void ScheduleInject() {
    if (!ShouldInject(web_contents()))
      return;
    inject_attempts_ = 0;
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(&GrokFabInjector::MaybeInject,
                                  weak_factory_.GetWeakPtr()));
  }

  void ScheduleStartupBurst() {
    static constexpr int kBurstDelaysMs[] = {100,  400,  1000, 2000,
                                             4000, 8000, 15000};
    for (int delay_ms : kBurstDelaysMs) {
      base::SequencedTaskRunner::GetCurrentDefault()->PostDelayedTask(
          FROM_HERE,
          base::BindOnce(&GrokFabInjector::BurstInject,
                         weak_factory_.GetWeakPtr()),
          base::Milliseconds(delay_ms));
    }
  }

  void MaybeScheduleGrokWebBurst() {
    content::WebContents* contents = web_contents();
    if (!contents)
      return;
    const GURL url = contents->GetLastCommittedURL();
    if (url.host().find("grok.com") == std::string::npos)
      return;
    if (url.query().find("xbrowser_grok") == std::string::npos &&
        url.ref().find("xbrowser_grok") == std::string::npos) {
      return;
    }
    ScheduleGrokWebSubmitBurst();
  }

  void ScheduleGrokWebSubmitBurst() {
    static constexpr int kBurstDelaysMs[] = {500,  1500, 3000, 5000,
                                             8000, 12000, 20000};
    for (int delay_ms : kBurstDelaysMs) {
      base::SequencedTaskRunner::GetCurrentDefault()->PostDelayedTask(
          FROM_HERE,
          base::BindOnce(&GrokFabInjector::BurstGrokWebSubmit,
                         weak_factory_.GetWeakPtr()),
          base::Milliseconds(delay_ms));
    }
  }

  void BurstGrokWebSubmit() {
    MaybeInject();
    MaybeInjectGrokWebSubmit();
  }

  void BurstInject() {
    if (!ShouldInject(web_contents()))
      return;
    MaybeInject();
  }

  void MaybeInject() {
    content::WebContents* contents = web_contents();
    if (!contents || !ShouldInject(contents))
      return;

    content::RenderFrameHost* frame = contents->GetPrimaryMainFrame();
    if (!frame || !frame->IsRenderFrameLive()) {
      RetryInject();
      return;
    }

    frame->ExecuteJavaScriptInIsolatedWorld(
        base::UTF8ToUTF16(BuildFabInjectScript()),
        base::BindOnce(&GrokFabInjector::OnInjected,
                       weak_factory_.GetWeakPtr()),
        ISOLATED_WORLD_ID_CHROME_INTERNAL);
  }

  void OnInjected(base::Value) {
    inject_attempts_ = 0;
    MaybeInjectGrokWebSubmit();
  }

  void MaybeInjectGrokWebSubmit() {
    content::WebContents* contents = web_contents();
    if (!contents)
      return;
    const GURL url = contents->GetLastCommittedURL();
    if (url.host().find("grok.com") == std::string::npos)
      return;
    const std::string id = ExtractGrokWebPendingId(url);
    if (id.empty() || grok_web_done_ids_.count(id))
      return;

    const std::string prompt = agent_gateway::GetGrokWebPendingPrompt(id);
    if (prompt.empty()) {
      LOG(WARNING) << "[grok-web] no pending prompt for id=" << id;
      return;
    }

    content::RenderFrameHost* frame = contents->GetPrimaryMainFrame();
    if (!frame || !frame->IsRenderFrameLive()) {
      RetryGrokWebSubmit();
      return;
    }

    LOG(INFO) << "[grok-web] injecting submit id=" << id
              << " prompt_chars=" << prompt.size();
    frame->ExecuteJavaScriptInIsolatedWorld(
        base::UTF8ToUTF16(BuildGrokWebNativeSubmitScript(prompt, id)),
        base::BindOnce(&GrokFabInjector::OnGrokWebSubmitInjected,
                       weak_factory_.GetWeakPtr(), id),
        ISOLATED_WORLD_ID_CHROME_INTERNAL);
  }

  void OnGrokWebSubmitInjected(const std::string& id, base::Value) {
    grok_web_submit_attempts_ = 0;
    CheckGrokWebSubmitDone(id, 0);
  }

  void CheckGrokWebSubmitDone(const std::string& id, int attempt) {
    if (grok_web_done_ids_.count(id))
      return;
    if (attempt > 120) {
      LOG(WARNING) << "[grok-web] submit check timed out id=" << id;
      return;
    }
    content::WebContents* contents = web_contents();
    if (!contents)
      return;
    content::RenderFrameHost* frame = contents->GetPrimaryMainFrame();
    if (!frame || !frame->IsRenderFrameLive()) {
      base::SequencedTaskRunner::GetCurrentDefault()->PostDelayedTask(
          FROM_HERE,
          base::BindOnce(&GrokFabInjector::CheckGrokWebSubmitDone,
                         weak_factory_.GetWeakPtr(), id, attempt + 1),
          base::Milliseconds(500));
      return;
    }
    const std::string script = base::StringPrintf(
        "(function(){return window.__xbrowserGrokWebSubmitDone||'';})()");
    frame->ExecuteJavaScriptInIsolatedWorld(
        base::UTF8ToUTF16(script),
        base::BindOnce(&GrokFabInjector::OnGrokWebSubmitCheckResult,
                       weak_factory_.GetWeakPtr(), id, attempt),
        ISOLATED_WORLD_ID_CHROME_INTERNAL);
  }

  void OnGrokWebSubmitCheckResult(const std::string& id,
                                  int attempt,
                                  base::Value result) {
    if (grok_web_done_ids_.count(id))
      return;
    std::string done;
    if (result.is_string())
      done = result.GetString();
    if (done == id) {
      LOG(INFO) << "[grok-web] submit confirmed id=" << id;
      agent_gateway::ConsumeGrokWebPendingPrompt(id);
      grok_web_done_ids_.insert(id);
      return;
    }
    base::SequencedTaskRunner::GetCurrentDefault()->PostDelayedTask(
        FROM_HERE,
        base::BindOnce(&GrokFabInjector::CheckGrokWebSubmitDone,
                       weak_factory_.GetWeakPtr(), id, attempt + 1),
        base::Milliseconds(500));
  }

  void RetryGrokWebSubmit() {
    if (++grok_web_submit_attempts_ > kMaxInjectAttempts)
      return;
    base::SequencedTaskRunner::GetCurrentDefault()->PostDelayedTask(
        FROM_HERE,
        base::BindOnce(&GrokFabInjector::MaybeInjectGrokWebSubmit,
                       weak_factory_.GetWeakPtr()),
        base::Milliseconds(250 * grok_web_submit_attempts_));
  }

  void RetryInject() {
    if (++inject_attempts_ > kMaxInjectAttempts)
      return;
    base::SequencedTaskRunner::GetCurrentDefault()->PostDelayedTask(
        FROM_HERE,
        base::BindOnce(&GrokFabInjector::MaybeInject,
                       weak_factory_.GetWeakPtr()),
        base::Milliseconds(250 * inject_attempts_));
  }

  int inject_attempts_ = 0;
  int grok_web_submit_attempts_ = 0;
  std::set<std::string> grok_web_done_ids_;
  base::WeakPtrFactory<GrokFabInjector> weak_factory_{this};

  WEB_CONTENTS_USER_DATA_KEY_DECL();
};

WEB_CONTENTS_USER_DATA_KEY_IMPL(GrokFabInjector);

}  // namespace

void AttachGrokFabInjector(content::WebContents* contents) {
  GrokFabInjector::Attach(contents);
  if (auto* injector = GrokFabInjector::FromWebContents(contents))
    injector->RefreshInjection();
}

void RegisterGrokFab(BrowserWindowInterface* browser) {
  if (!browser || !browser->GetProfile() ||
      !browser->GetProfile()->IsRegularProfile()) {
    return;
  }
  TabStripModel* model = browser->GetTabStripModel();
  if (!model)
    return;
  for (int i = 0; i < model->count(); ++i)
    AttachGrokFabInjector(model->GetWebContentsAt(i));
}

}  // namespace grok_companion