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

bool IsGrokWebHost(const GURL& url) {
  return IsGrokToolbarOverlayHost(url);
}

bool IsFabHost(const GURL& url) {
  if (!url.is_valid() || !url.SchemeIsHTTPOrHTTPS())
    return false;
  if (url.host() == kCompanionHost &&
      url.EffectiveIntPort() == GatewayPort()) {
    return false;
  }
  if (IsGrokWebHost(url))
    return false;
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
  constexpr char kKey[] = "xplorer_grok=";
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
  console.log('[xplorer] native inject grok-web id='+id+' prompt_len='+p.length);
  if(window.__xplorerNativeGrokSubmit){
    window.__xplorerNativeGrokSubmit(p,id);
  }else{
    window.__xplorerGrokWebNativeQueue=window.__xplorerGrokWebNativeQueue||[];
    window.__xplorerGrokWebNativeQueue.push({prompt:p,id:id});
    console.log('[xplorer] queued native grok-web (fab script not ready)');
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
  var WRAP_ID='xplorer-grok-wrap',FAB_ID='xplorer-grok-fab',MENU_ID='xplorer-grok-menu',STYLE_ID='xplorer-grok-fab-style';
  var GROK_PATH='M12.745 20.54l10.97-8.19c.539-.4 1.307-.244 1.564.38 1.349 3.288.746 7.241-1.938 9.955-2.683 2.714-6.417 3.31-9.83 1.954l-3.728 1.745c5.347 3.697 11.84 2.782 15.898-1.324 3.219-3.255 4.216-7.692 3.284-11.693l.008.009c-1.351-5.878.332-8.227 3.782-13.031L33 0l-4.54 4.59v-.014L12.743 20.544m-2.263 1.987c-3.837-3.707-3.175-9.446.1-12.755 2.42-2.449 6.388-3.448 9.852-1.979l3.72-1.737c-.67-.49-1.53-1.017-2.515-1.387-4.455-1.854-9.789-.931-13.41 2.728-3.483 3.523-4.579 8.94-2.697 13.561 1.405 3.454-.899 5.898-3.22 8.364C1.49 30.2.666 31.074 0 32l10.478-9.466';
  function clearNode(node){while(node&&node.firstChild)node.removeChild(node.firstChild);}
  function createGrokIcon(){
    var svg=document.createElementNS('http://www.w3.org/2000/svg','svg');
    svg.setAttribute('viewBox','0 0 33 32');
    svg.setAttribute('aria-hidden','true');
    svg.setAttribute('class','xfab-grok-icon');
    var path=document.createElementNS('http://www.w3.org/2000/svg','path');
    path.setAttribute('d',GROK_PATH);
    svg.appendChild(path);
    return svg;
  }
  function appendMenuItem(menu,action,icon,label,chevron){
    var btn=document.createElement('button');
    btn.type='button';
    btn.className='xfab-menu-item';
    btn.setAttribute('data-action',action);
    var ic=document.createElement('span');
    ic.className='xfab-menu-icon'+(action==='open'?' xfab-menu-grok-mark':'');
    ic.textContent=icon;
    btn.appendChild(ic);
    var lb=document.createElement('span');
    lb.className='xfab-menu-label';
    lb.textContent=label;
    btn.appendChild(lb);
    if(chevron){
      var ch=document.createElement('span');
      ch.className='xfab-menu-chevron';
      ch.textContent=chevron;
      btn.appendChild(ch);
    }
    menu.appendChild(btn);
    return btn;
  }
  function detectAppContext(){
    var href=location.href||'';
    var run=href.match(/\/run\/([^/?#]+)/);
    if(run&&run[1])return {id:run[1]};
    var m=href.match(/^https?:\/\/127\.0\.0\.1:(\d+)(?:\/|$|\?|#)/);
    if(!m)return null;
    var port=parseInt(m[1],10);
    if(!port||port===parseInt(String(new URL(GW).port||'9334'),10))return null;
    return {port:port};
  }
  function resolveAppFromContext(ctx,apps){
    if(!ctx||!apps)return null;
    if(ctx.id)return apps.find(function(a){return a.id===ctx.id;})||null;
    if(ctx.port)return apps.find(function(a){return a.runtime_port===ctx.port;})||null;
    return null;
  }
  function buildMenu(menu){
    clearNode(menu);
    var appCtx=detectAppContext();
    if(appCtx){
      appendMenuItem(menu,'openappbuilder','\u2699','Open in builder');
      appendMenuItem(menu,'modifyapp','\u270f','Modify app');
      appendMenuItem(menu,'renameapp','\u270e','Rename app');
      appendMenuItem(menu,'duplicateapp','\u2398','Duplicate app');
      appendMenuItem(menu,'shareapp','\u29c9','Copy app link');
      appendMenuItem(menu,'copybuilderlink','\u2699','Copy builder link');
      appendMenuItem(menu,'exportapp','\u2b07','Export app');
    }
    appendMenuItem(menu,'analyze','\u2726','Analyze with Grok');
    appendMenuItem(menu,'summarize','\u21b3','Summarize this','\u203a');
    appendMenuItem(menu,'factcheck','\u2713','Is this true?');
    appendMenuItem(menu,'explain','\u2026','Explain this');
    appendMenuItem(menu,'buildapp','\u2699','Build app from page');
    appendMenuItem(menu,'settings','\u2699','Grok settings');
    appendMenuItem(menu,'open','\u2726','Open in Grok');
  }
  function buildFabButton(fab){
    if(fab.querySelector('.xfab-grok-icon'))return;
    clearNode(fab);
    fab.appendChild(createGrokIcon());
    var label=document.createElement('span');
    label.textContent='Grok';
    fab.appendChild(label);
  }
  var css='#xplorer-grok-wrap{position:fixed;bottom:20px;right:20px;z-index:2147483646;display:flex;flex-direction:column;align-items:flex-end;gap:0;font:13px/1.4 -apple-system,BlinkMacSystemFont,sans-serif}'
    +'#xplorer-grok-wrap::before{content:"";position:absolute;left:0;right:0;bottom:100%%;height:12px}'
    +'#xplorer-grok-menu{display:none;flex-direction:column;min-width:240px;margin-bottom:8px;padding:6px;background:#fff;border:1px solid #eff3f4;border-radius:16px;box-shadow:0 8px 28px rgba(15,20,25,.18);overflow:hidden}'
    +'#xplorer-grok-menu.open{display:flex}'
    +'.xfab-menu-item{display:flex;align-items:center;gap:12px;width:100%%;border:none;background:transparent;padding:12px 14px;text-align:left;font:inherit;font-size:15px;color:#0f1419;cursor:pointer;border-radius:10px}'
    +'.xfab-menu-item:hover{background:#f7f9f9}'
    +'.xfab-menu-item:disabled{opacity:.5;cursor:wait}'
    +'.xfab-menu-icon{width:20px;text-align:center;font-size:16px;color:#536471;flex-shrink:0}'
    +'.xfab-menu-grok-mark{color:#0f1419}'
    +'.xfab-menu-label{flex:1}'
    +'.xfab-menu-chevron{color:#536471;font-size:14px}'
    +'#xplorer-grok-fab{display:inline-flex;align-items:center;gap:6px;border:1px solid #cfd9de;border-radius:999px;background:#fff;color:#0f1419;padding:6px 12px 6px 10px;font:600 13px/1 -apple-system,BlinkMacSystemFont,sans-serif;cursor:pointer;box-shadow:0 2px 10px rgba(15,20,25,.12);transition:transform .15s,box-shadow .15s,background .15s,border-color .15s}'
    +'#xplorer-grok-fab:hover{background:#f7f9f9;border-color:#aab8c2;transform:translateY(-1px);box-shadow:0 4px 14px rgba(15,20,25,.16)}'
    +'#xplorer-grok-fab:disabled{opacity:.65;cursor:wait;transform:none}'
    +'.xfab-grok-icon{width:15px;height:15px;fill:currentColor;display:block;flex-shrink:0}'
    +'@media (prefers-color-scheme:dark){'
    +'#xplorer-grok-fab{background:#000;color:#fff;border-color:#2f3336;box-shadow:0 2px 12px rgba(0,0,0,.45)}'
    +'#xplorer-grok-fab:hover{background:#16181c;border-color:#536471}'
    +'#xplorer-grok-menu{background:#000;border-color:#2f3336;box-shadow:0 8px 28px rgba(0,0,0,.55)}'
    +'.xfab-menu-item{color:#e7e9ea}'
    +'.xfab-menu-item:hover{background:#16181c}'
    +'.xfab-menu-icon,.xfab-menu-chevron,.xfab-menu-grok-mark{color:#e7e9ea}'
    +'}';
  var state=window.__xplorerGrokFabState||(window.__xplorerGrokFabState={busy:false,pageData:null});
  function xlog(){
    try{console.log.apply(console,['[xplorer-fab]'].concat(Array.prototype.slice.call(arguments)));}catch(e){}
  }
  function extractPage(){
    var kill='script,style,noscript,svg,nav,footer,aside,iframe,#xplorer-grok-bar,#xplorer-grok-wrap,#xplorer-grok-fab,#xplorer-grok-menu,[data-aether-hud]';
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
  function isGrokWebPage(){
    var host=(location.hostname||'').toLowerCase();
    return host.indexOf('grok.com')>=0||host.indexOf('grokipedia.com')>=0||
      host==='x.com'||host.endsWith('.x.com')||
      host==='twitter.com'||host.endsWith('.twitter.com');
  }
  function ensureFab(){
    if(!document.documentElement)return;
    if(isGrokWebPage())return;
    var style=document.getElementById(STYLE_ID);
    if(!style){
      style=document.createElement('style');
      style.id=STYLE_ID;
      document.documentElement.appendChild(style);
    }
    style.textContent=css;
    var wrap=document.getElementById(WRAP_ID);
    if(!wrap){
      wrap=document.createElement('div');
      wrap.id=WRAP_ID;
      document.documentElement.appendChild(wrap);
    }else if(wrap.parentNode!==document.documentElement){
      document.documentElement.appendChild(wrap);
    }
    var menu=document.getElementById(MENU_ID);
    if(!menu){
      menu=document.createElement('div');
      menu.id=MENU_ID;
      wrap.appendChild(menu);
    }
    var fab=document.getElementById(FAB_ID);
    if(!fab){
      fab=document.createElement('button');
      fab.id=FAB_ID;
      fab.type='button';
      wrap.appendChild(fab);
    }else if(fab.parentNode!==wrap){
      wrap.appendChild(fab);
    }
    if(menu.dataset.version!=='14'){
      menu.dataset.version='14';
      buildMenu(menu);
      fab.dataset.wired='';
    }
    fab.title='Grok this page';
    fab.setAttribute('aria-label','Grok this page');
    buildFabButton(fab);
    var oldPanel=document.getElementById('xplorer-grok-panel');
    if(oldPanel)oldPanel.remove();
    if(fab.dataset.wired==='14')return;
    fab.dataset.wired='14';
    function setBusy(on){
      state.busy=on;
      fab.disabled=on;
      menu.querySelectorAll('.xfab-menu-item').forEach(function(btn){btn.disabled=on;});
    }
    function toggleMenu(open){
      if(open===undefined)open=!menu.classList.contains('open');
      if(open)buildMenu(menu);
      menu.classList.toggle('open',open);
    }
    function runGrokWeb(action){
      if(state.busy)return;
      toggleMenu(false);
      if(action==='open'){
        setBusy(true);
        window.open('https://grok.com/','_blank');
        setBusy(false);
        return;
      }
      state.pageData=extractPage();
      if(action==='renameapp'){
        var ctx=detectAppContext();
        if(!ctx){
          alert('Not viewing an Xplorer app.');
          return;
        }
        setBusy(true);
        fetch(GW+'/api/apps').then(function(r){return r.json();}).then(function(d){
          if(d.error)throw new Error(d.error);
          var app=resolveAppFromContext(ctx,d.apps||[]);
          if(!app)throw new Error('App not found');
          var next=window.prompt('Rename app',app.name||'App');
          if(!next||!next.trim())return Promise.resolve();
          return fetch(GW+'/api/apps/'+encodeURIComponent(app.id)+'/rename',{
            method:'POST',
            headers:{'Content-Type':'application/json'},
            body:JSON.stringify({name:next.trim()})
          }).then(function(r){return r.json().then(function(j){
            if(!r.ok)throw new Error(j.error||'rename failed');
            document.title=next.trim();
          });});
        }).catch(function(e){
          if(e&&e.message)alert('Rename failed: '+e.message);
        }).finally(function(){setBusy(false);});
        return;
      }
      if(action==='copybuilderlink'){
        var ctx=detectAppContext();
        if(!ctx){
          alert('Not viewing an Xplorer app.');
          return;
        }
        setBusy(true);
        fetch(GW+'/api/apps').then(function(r){return r.json();}).then(function(d){
          if(d.error)throw new Error(d.error);
          var app=resolveAppFromContext(ctx,d.apps||[]);
          if(!app)throw new Error('App not found');
          var link=GW+'/app?id='+encodeURIComponent(app.id);
          if(navigator.clipboard&&navigator.clipboard.writeText){
            return navigator.clipboard.writeText(link).then(function(){
              alert('Builder link copied');
            });
          }
          window.prompt('Copy builder link:',link);
        }).catch(function(e){
          alert('Copy failed: '+(e.message||e));
        }).finally(function(){setBusy(false);});
        return;
      }
      if(action==='shareapp'){
        var ctx=detectAppContext();
        if(!ctx){
          alert('Not viewing an Xplorer app.');
          return;
        }
        setBusy(true);
        fetch(GW+'/api/apps').then(function(r){return r.json();}).then(function(d){
          if(d.error)throw new Error(d.error);
          var app=resolveAppFromContext(ctx,d.apps||[]);
          var link=(app&&(app.runtime_url||app.open_url))||location.href;
          if(navigator.clipboard&&navigator.clipboard.writeText){
            return navigator.clipboard.writeText(link).then(function(){
              alert('App link copied');
            });
          }
          window.prompt('Copy app link:',link);
        }).catch(function(e){
          alert('Share failed: '+(e.message||e));
        }).finally(function(){setBusy(false);});
        return;
      }
      if(action==='modifyapp'){
        var ctx=detectAppContext();
        if(!ctx){
          alert('Not viewing an Xplorer app.');
          return;
        }
        var prompt=window.prompt('What should Grok change in this app?','');
        if(!prompt||!prompt.trim())return;
        setBusy(true);
        fetch(GW+'/api/apps').then(function(r){return r.json();}).then(function(d){
          if(d.error)throw new Error(d.error);
          var app=resolveAppFromContext(ctx,d.apps||[]);
          if(!app)throw new Error('App not found');
          sessionStorage.setItem('xplorer_app_build',JSON.stringify({
            id:app.id,prompt:prompt.trim()
          }));
          window.open(GW+'/app?id='+encodeURIComponent(app.id)+'&autobuild=1','_blank');
        }).catch(function(e){
          alert('Modify failed: '+(e.message||e));
        }).finally(function(){setBusy(false);});
        return;
      }
      if(action==='duplicateapp'){
        var ctx=detectAppContext();
        if(!ctx){
          alert('Not viewing an Xplorer app.');
          return;
        }
        setBusy(true);
        fetch(GW+'/api/apps').then(function(r){return r.json();}).then(function(d){
          if(d.error)throw new Error(d.error);
          var app=resolveAppFromContext(ctx,d.apps||[]);
          if(!app)throw new Error('App not found');
          return fetch(GW+'/api/apps/'+encodeURIComponent(app.id)+'/duplicate',{
            method:'POST',
            headers:{'Content-Type':'application/json'},
            body:'{}'
          }).then(function(r){return r.json().then(function(j){
            if(!r.ok)throw new Error(j.error||'duplicate failed');
            if(j.app&&j.app.id)window.open(GW+'/app?id='+encodeURIComponent(j.app.id),'_blank');
          });});
        }).catch(function(e){
          alert('Duplicate failed: '+(e.message||e));
        }).finally(function(){setBusy(false);});
        return;
      }
      if(action==='openappbuilder'){
        var ctx=detectAppContext();
        if(!ctx){
          alert('Not viewing an Xplorer app.');
          return;
        }
        setBusy(true);
        fetch(GW+'/api/apps').then(function(r){return r.json();}).then(function(d){
          if(d.error)throw new Error(d.error);
          var app=resolveAppFromContext(ctx,d.apps||[]);
          if(!app)throw new Error('App not found');
          window.open(GW+'/app?id='+encodeURIComponent(app.id),'_blank');
        }).catch(function(e){
          alert('Open builder failed: '+(e.message||e));
        }).finally(function(){setBusy(false);});
        return;
      }
      if(action==='exportapp'){
        var ctx=detectAppContext();
        if(!ctx){
          alert('Not viewing an Xplorer app.');
          return;
        }
        setBusy(true);
        fetch(GW+'/api/apps').then(function(r){return r.json();}).then(function(d){
          if(d.error)throw new Error(d.error);
          var app=resolveAppFromContext(ctx,d.apps||[]);
          if(!app||!app.exportable)throw new Error('App not exportable');
          return fetch(GW+'/api/apps/'+encodeURIComponent(app.id)+'/export').then(function(r){
            if(!r.ok)return r.json().then(function(e){throw new Error(e.error||'export failed');});
            return r.blob().then(function(blob){
              var slug=(app.name||'app').replace(/[^\w.-]+/g,'_').slice(0,48);
              var a=document.createElement('a');
              a.href=URL.createObjectURL(blob);
              a.download=slug+'.zip';
              a.click();
              URL.revokeObjectURL(a.href);
            });
          });
        }).catch(function(e){
          alert('Export failed: '+(e.message||e));
        }).finally(function(){setBusy(false);});
        return;
      }
      if(action==='settings'){
        window.open(GW+'/settings','_blank');
        return;
      }
      if(action==='buildapp'){
        if(!state.pageData.text){
          alert('No readable text on this page.');
          return;
        }
        setBusy(true);
        var title=(state.pageData.title||'Page app').slice(0,40);
        var prompt='Build a small interactive web app inspired by this page. Page: '+
          state.pageData.title+'\nURL: '+state.pageData.url+'\n\nContent:\n'+
          state.pageData.text.slice(0,12000);
        fetch(GW+'/api/apps',{
          method:'POST',
          headers:{'Content-Type':'application/json'},
          body:JSON.stringify({prompt:prompt,name:title})
        }).then(function(r){return r.json();}).then(function(d){
          if(d.error)throw new Error(d.error);
          if(!d.app||!d.app.id)throw new Error('missing app id');
          sessionStorage.setItem('xplorer_app_build',JSON.stringify({
            id:d.app.id,prompt:prompt
          }));
          window.open(GW+'/app?id='+encodeURIComponent(d.app.id)+'&autobuild=1','_blank');
        }).catch(function(e){
          alert('Build app failed: '+(e.message||e));
        }).finally(function(){setBusy(false);});
        return;
      }
      if(!state.pageData.text){
        alert('No readable text on this page.');
        return;
      }
      setBusy(true);
      xlog('grok web',action,state.pageData.source,state.pageData.text.length);
      var payload={
        url:state.pageData.url,
        title:state.pageData.title,
        text:state.pageData.text,
        action:action||'summarize'
      };
      fetch(GW+'/api/page/grok-web',{
        method:'POST',
        headers:{'Content-Type':'application/json'},
        body:JSON.stringify(payload)
      }).then(function(r){return r.json();}).then(function(d){
        if(d.error)throw new Error(d.error);
        if(d.grok_url)window.open(d.grok_url,'_blank');
        else throw new Error('missing grok_url');
      }).catch(function(e){
        alert('Grok Web failed: '+(e.message||e));
      }).finally(function(){setBusy(false);});
    }
    fab.onclick=function(e){
      e.stopPropagation();
      toggleMenu();
    };
    menu.querySelectorAll('[data-action]').forEach(function(btn){
      btn.onclick=function(e){
        e.stopPropagation();
        runGrokWeb(btn.getAttribute('data-action'));
      };
    });
    if(!window.__xplorerGrokMenuClose){
      window.__xplorerGrokMenuClose=true;
      document.addEventListener('click',function(){toggleMenu(false);});
      wrap.addEventListener('click',function(e){e.stopPropagation();});
    }
  }
  function getGrokWebPendingId(){
    var id=new URLSearchParams(location.search).get('xplorer_grok');
    if(id)return id;
    id=new URLSearchParams(location.search).get('xbrowser_grok');
    if(id)return id;
    var m=(location.hash||'').match(/xplorer_grok=([^&]+)/);
    if(m)return decodeURIComponent(m[1]);
    m=(location.hash||'').match(/xbrowser_grok=([^&]+)/);
    return m?decodeURIComponent(m[1]):null;
  }
  function clearGrokWebPendingId(){
    try{
      var u=new URL(location.href);
      u.searchParams.delete('xplorer_grok');
      u.searchParams.delete('xbrowser_grok');
      u.hash=(u.hash||'').replace(/xplorer_grok=[^&]+&?/,'').replace(/xbrowser_grok=[^&]+&?/,'').replace(/&$/,'');
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
        'textarea[placeholder*="What do you want" i]',
        'textarea[placeholder*="Ask" i]','textarea[placeholder*="Grok" i]',
        'textarea[placeholder*="know" i]',
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
    if(!window.__xplorerGrokSubmitWatch){
      window.__xplorerGrokSubmitWatch=true;
      new MutationObserver(function(){if(!submitted)tryOnce();})
        .observe(document.documentElement,{childList:true,subtree:true});
    }
  }
  window.__xplorerNativeGrokSubmit=function(prompt,id){
    xlog('nativeGrokSubmit id='+id+' len='+prompt.length);
    if(window.__xplorerGrokConsuming===id||window.__xplorerGrokWebSubmitDone===id)return;
    window.__xplorerGrokConsuming=id;
    submitGrokWebPrompt(prompt,function(){
      xlog('grok-web submit done id='+id);
      window.__xplorerGrokWebSubmitDone=id;
      fetch(GW+'/api/page/grok-web/consumed?id='+encodeURIComponent(id),{method:'POST'})
        .then(function(){xlog('consumed ok');})
        .catch(function(e){xlog('consumed fetch failed',e);});
      clearGrokWebPendingId();
      window.__xplorerGrokConsuming='';
    });
  };
  if(window.__xplorerGrokWebNativeQueue){
    window.__xplorerGrokWebNativeQueue.forEach(function(q){
      window.__xplorerNativeGrokSubmit(q.prompt,q.id);
    });
    window.__xplorerGrokWebNativeQueue=[];
  }
  function tryConsumeGrokWebPending(){
    var host=location.hostname||'';
    if(host.indexOf('grok.com')<0&&host.indexOf('.grok.com')<0)return;
    var id=getGrokWebPendingId();
    if(!id)return;
    if(window.__xplorerGrokConsuming===id||window.__xplorerGrokWebSubmitDone===id)return;
    xlog('fetch fallback pending id='+id);
    window.__xplorerGrokConsuming=id;
    fetch(GW+'/api/page/grok-web/pending?id='+encodeURIComponent(id))
      .then(function(r){return r.json();})
      .then(function(d){
        if(!d.prompt){
          xlog('fetch fallback: no prompt',d);
          window.__xplorerGrokConsuming='';
          return;
        }
        submitGrokWebPrompt(d.prompt,function(){
          window.__xplorerGrokWebSubmitDone=id;
          fetch(GW+'/api/page/grok-web/consumed?id='+encodeURIComponent(id),{method:'POST'});
          clearGrokWebPendingId();
          window.__xplorerGrokConsuming='';
        });
      }).catch(function(e){
        xlog('fetch fallback failed',e);
        window.__xplorerGrokConsuming='';
      });
  }
  function fabNeedsMount(){
    if(isGrokWebPage())return false;
    var wrap=document.getElementById(WRAP_ID);
    return !wrap||wrap.parentNode!==document.documentElement;
  }
  ensureFab();
  tryConsumeGrokWebPending();
  if((location.hostname||'').indexOf('grok.com')>=0&&!window.__xplorerGrokConsumeWatch){
    window.__xplorerGrokConsumeWatch=true;
    var consumeAttempts=0;
    var consumeTimer=setInterval(function(){
      if(window.__xplorerGrokWebSubmitDone||!getGrokWebPendingId()){
        clearInterval(consumeTimer);
        return;
      }
      if(++consumeAttempts>24){
        clearInterval(consumeTimer);
        return;
      }
      tryConsumeGrokWebPending();
    },2500);
  }
  if(!window.__xplorerGrokFabWatch){
    window.__xplorerGrokFabWatch=true;
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
    if (!ExtractGrokWebPendingId(url).empty())
      ScheduleGrokWebSubmitBurst();
    if (!IsFabHost(url))
      return;
    ScheduleInject();
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
    if (IsGrokWebHost(contents->GetLastCommittedURL()) ||
        IsGrokWebHost(contents->GetVisibleURL())) {
      return true;
    }
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
    if (url.query().find("xplorer_grok") == std::string::npos &&
        url.ref().find("xplorer_grok") == std::string::npos) {
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
        "(function(){return window.__xplorerGrokWebSubmitDone||'';})()");
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