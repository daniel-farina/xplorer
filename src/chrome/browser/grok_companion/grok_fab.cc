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
  var FAB_ID='xbrowser-grok-fab',PANEL_ID='xbrowser-grok-panel',STYLE_ID='xbrowser-grok-fab-style';
  var GROK_ICON='<svg viewBox="0 0 33 32" aria-hidden="true" class="xfab-grok-icon"><g><path d="M12.745 20.54l10.97-8.19c.539-.4 1.307-.244 1.564.38 1.349 3.288.746 7.241-1.938 9.955-2.683 2.714-6.417 3.31-9.83 1.954l-3.728 1.745c5.347 3.697 11.84 2.782 15.898-1.324 3.219-3.255 4.216-7.692 3.284-11.693l.008.009c-1.351-5.878.332-8.227 3.782-13.031L33 0l-4.54 4.59v-.014L12.743 20.544m-2.263 1.987c-3.837-3.707-3.175-9.446.1-12.755 2.42-2.449 6.388-3.448 9.852-1.979l3.72-1.737c-.67-.49-1.53-1.017-2.515-1.387-4.455-1.854-9.789-.931-13.41 2.728-3.483 3.523-4.579 8.94-2.697 13.561 1.405 3.454-.899 5.898-3.22 8.364C1.49 30.2.666 31.074 0 32l10.478-9.466"></path></g></svg>';
  var css='#xbrowser-grok-fab{position:fixed;bottom:20px;right:20px;z-index:2147483646;display:inline-flex;align-items:center;justify-content:center;gap:6px;padding:7px 12px 7px 10px;border:1px solid #dadce0;border-radius:999px;background:#fff;color:#202124;font:500 12px/1 -apple-system,BlinkMacSystemFont,sans-serif;cursor:pointer;box-shadow:0 2px 8px rgba(60,64,67,.15);white-space:nowrap;-webkit-font-smoothing:antialiased;transition:transform .15s,box-shadow .15s,background .15s}'
    +'.xfab-grok-icon{width:14px;height:14px;flex-shrink:0;display:block;fill:currentColor}'
    +'#xbrowser-grok-fab:hover{transform:translateY(-1px);box-shadow:0 4px 12px rgba(60,64,67,.2);background:#f8f9fa}'
    +'#xbrowser-grok-fab:disabled{opacity:.55;cursor:not-allowed;transform:none}'
    +'#xbrowser-grok-panel{position:fixed;bottom:62px;right:20px;z-index:2147483646;width:min(380px,calc(100vw - 40px));max-height:min(420px,60vh);display:none;flex-direction:column;background:#fff;color:#202124;border:1px solid #dadce0;border-radius:14px;box-shadow:0 8px 28px rgba(60,64,67,.18);font:13px/1.45 -apple-system,BlinkMacSystemFont,sans-serif;overflow:hidden;transition:width .2s,max-height .2s}'
    +'#xbrowser-grok-panel.expanded{width:min(640px,calc(100vw - 24px));max-height:min(78vh,720px)}'
    +'#xbrowser-grok-panel.open{display:flex}'
    +'.xfab-head{display:flex;align-items:center;justify-content:space-between;gap:8px;padding:12px 14px;border-bottom:1px solid #dadce0;font-weight:600;font-size:14px;color:#202124}'
    +'.xfab-head-actions{display:inline-flex;align-items:center;gap:4px}'
    +'.xfab-icon-btn,.xfab-close{border:none;background:transparent;color:#5f6368;font-size:16px;cursor:pointer;padding:2px 6px;line-height:1;border-radius:6px}'
    +'.xfab-icon-btn:hover,.xfab-close:hover{background:#f1f3f4;color:#202124}'
    +'.xfab-picker{padding:14px;display:flex;flex-direction:column;gap:10px}'
    +'.xfab-picker-label{margin:0;font-size:13px;color:#5f6368}'
    +'.xfab-choice{display:flex;flex-direction:column;align-items:flex-start;gap:2px;width:100%%;border:1px solid #dadce0;background:#f8f9fa;border-radius:12px;padding:10px 12px;cursor:pointer;text-align:left;font:inherit;transition:border-color .15s,background .15s}'
    +'.xfab-choice:hover{border-color:#1a73e8;background:#fff}'
    +'.xfab-choice-title{font-weight:600;font-size:13px;color:#202124}'
    +'.xfab-choice-desc{font-size:12px;color:#5f6368}'
    +'.xfab-body{padding:12px 14px;overflow-y:auto;flex:1;white-space:pre-wrap;word-break:break-word;color:#3c4043}'
    +'.xfab-body.loading{color:#5f6368;font-style:italic}'
    +'.xfab-body[hidden],.xfab-picker[hidden],.xfab-actions[hidden]{display:none!important}'
    +'.xfab-actions{display:flex;gap:8px;padding:10px 14px 14px;border-top:1px solid #dadce0}'
    +'.xfab-btn{flex:1;border:1px solid #dadce0;background:#f8f9fa;color:#202124;border-radius:10px;padding:8px 12px;font:inherit;font-size:12px;font-weight:500;cursor:pointer}'
    +'.xfab-btn:hover{border-color:#1a73e8;color:#1a73e8;background:#fff}'
    +'.xfab-btn.primary{background:#1a73e8;border-color:#1a73e8;color:#fff}'
    +'.xfab-btn.primary:hover{background:#1557b0;border-color:#1557b0;color:#fff}'
    +'.xfab-btn:disabled{opacity:.45;cursor:not-allowed}'
    +'@media (prefers-color-scheme:dark){'
    +'#xbrowser-grok-fab{background:#292a2d;color:#e8eaed;border-color:#3c4043;box-shadow:0 2px 10px rgba(0,0,0,.35)}'
    +'#xbrowser-grok-fab:hover{background:#35363a;box-shadow:0 4px 14px rgba(0,0,0,.45)}'
    +'#xbrowser-grok-panel{background:#292a2d;color:#e8eaed;border-color:#3c4043;box-shadow:0 8px 32px rgba(0,0,0,.45)}'
    +'.xfab-head{border-bottom-color:#3c4043;color:#e8eaed}'
    +'.xfab-icon-btn,.xfab-close{color:#9aa0a6}'
    +'.xfab-icon-btn:hover,.xfab-close:hover{background:#35363a;color:#e8eaed}'
    +'.xfab-picker-label{color:#9aa0a6}'
    +'.xfab-choice{background:#35363a;border-color:#3c4043}'
    +'.xfab-choice:hover{border-color:#8ab4f8;background:#292a2d}'
    +'.xfab-choice-title{color:#e8eaed}'
    +'.xfab-choice-desc{color:#9aa0a6}'
    +'.xfab-body{color:#bdc1c6}'
    +'.xfab-body.loading{color:#9aa0a6}'
    +'.xfab-actions{border-top-color:#3c4043}'
    +'.xfab-btn{background:#35363a;color:#e8eaed;border-color:#3c4043}'
    +'.xfab-btn:hover{border-color:#8ab4f8;color:#8ab4f8;background:#292a2d}'
    +'.xfab-btn.primary{background:#8ab4f8;border-color:#8ab4f8;color:#202124}'
    +'.xfab-btn.primary:hover{background:#aecbfa;border-color:#aecbfa;color:#202124}'
    +'}';
  var state=window.__xbrowserGrokFabState||(window.__xbrowserGrokFabState={summary:'',busy:false,pageData:null});
  function xlog(){
    try{console.log.apply(console,['[xbrowser-fab]'].concat(Array.prototype.slice.call(arguments)));}catch(e){}
  }
  function extractPage(){
    var kill='script,style,noscript,svg,nav,footer,aside,iframe,#xbrowser-grok-bar,#xbrowser-grok-fab,#xbrowser-grok-panel,[data-aether-hud]';
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
    fab.title='Grok this page';
    fab.setAttribute('aria-label','Grok this page');
    fab.innerHTML=GROK_ICON+'<span>Grok it</span>';
    var panel=document.getElementById(PANEL_ID);
    if(!panel){
      panel=document.createElement('div');
      panel.id=PANEL_ID;
      document.documentElement.appendChild(panel);
    }else if(panel.parentNode!==document.documentElement){
      document.documentElement.appendChild(panel);
    }
    if(panel.dataset.version!=='3'){
      panel.dataset.version='3';
      panel.innerHTML='<div class="xfab-head"><span class="xfab-title">Grok this page</span>'
        +'<div class="xfab-head-actions">'
        +'<button type="button" class="xfab-icon-btn" id="xfab-expand" aria-label="Expand panel" title="Expand">\u2922</button>'
        +'<button type="button" class="xfab-close" aria-label="Close">\u00d7</button>'
        +'</div></div>'
        +'<div class="xfab-picker" id="xfab-picker">'
        +'<p class="xfab-picker-label">How would you like to Grok it?</p>'
        +'<button type="button" class="xfab-choice" id="xfab-build">'
        +'<span class="xfab-choice-title">Grok Build</span>'
        +'<span class="xfab-choice-desc">Summarize in this panel</span></button>'
        +'<button type="button" class="xfab-choice" id="xfab-web">'
        +'<span class="xfab-choice-title">Grok Web</span>'
        +'<span class="xfab-choice-desc">Open grok.com with this page</span></button>'
        +'</div>'
        +'<div class="xfab-body loading" id="xfab-body" hidden></div>'
        +'<div class="xfab-actions" id="xfab-actions" hidden>'
        +'<button type="button" class="xfab-btn" id="xfab-copy" disabled>Copy</button>'
        +'<button type="button" class="xfab-btn primary" id="xfab-chat" disabled>Chat in Build</button>'
        +'</div>';
      fab.dataset.wired='';
    }
    if(fab.dataset.wired==='1')return;
    fab.dataset.wired='1';
    var pickerEl=panel.querySelector('#xfab-picker');
    var bodyEl=panel.querySelector('#xfab-body');
    var actionsEl=panel.querySelector('#xfab-actions');
    var copyBtn=panel.querySelector('#xfab-copy');
    var chatBtn=panel.querySelector('#xfab-chat');
    var closeBtn=panel.querySelector('.xfab-close');
    var expandBtn=panel.querySelector('#xfab-expand');
    var buildBtn=panel.querySelector('#xfab-build');
    var webBtn=panel.querySelector('#xfab-web');
    function setBusy(on){
      state.busy=on;
      fab.disabled=on;
      if(copyBtn)copyBtn.disabled=on||!state.summary;
      if(chatBtn)chatBtn.disabled=on||!state.summary;
      if(buildBtn)buildBtn.disabled=on;
      if(webBtn)webBtn.disabled=on;
    }
    function togglePanel(open){
      if(open===undefined)open=!panel.classList.contains('open');
      panel.classList.toggle('open',open);
    }
    function showPicker(){
      if(pickerEl)pickerEl.hidden=false;
      if(bodyEl){bodyEl.hidden=true;bodyEl.classList.add('loading');}
      if(actionsEl)actionsEl.hidden=true;
    }
    function showResults(){
      if(pickerEl)pickerEl.hidden=true;
      if(bodyEl)bodyEl.hidden=false;
      if(actionsEl)actionsEl.hidden=false;
    }
    closeBtn.onclick=function(){togglePanel(false);};
    expandBtn.onclick=function(){
      var on=panel.classList.toggle('expanded');
      expandBtn.setAttribute('aria-label',on?'Shrink panel':'Expand panel');
      expandBtn.setAttribute('title',on?'Shrink':'Expand');
      expandBtn.textContent=on?'\u2923':'\u2922';
    };
    fab.onclick=function(){
      togglePanel(true);
      if(state.summary){showResults();return;}
      if(!state.busy)showPicker();
    };
    buildBtn.onclick=function(){runSummarize();};
    webBtn.onclick=function(){runGrokWeb();};
    copyBtn.onclick=function(){
      if(!state.summary)return;
      var done=function(){copyBtn.textContent='Copied!';setTimeout(function(){copyBtn.textContent='Copy';},1500);};
      if(navigator.clipboard&&navigator.clipboard.writeText){
        navigator.clipboard.writeText(state.summary).then(done).catch(function(){
          prompt('Copy summary:',state.summary);
        });
      }else{prompt('Copy summary:',state.summary);done();}
    };
    chatBtn.onclick=function(){
      if(!state.pageData||!state.summary||state.busy)return;
      setBusy(true);
      bodyEl.textContent='Opening chat…';
      fetch(GW+'/api/page/start-chat',{
        method:'POST',
        headers:{'Content-Type':'application/json'},
        body:JSON.stringify({url:state.pageData.url,title:state.pageData.title,text:state.pageData.text,summary:state.summary})
      }).then(function(r){return r.json();}).then(function(d){
        if(d.error)throw new Error(d.error);
        if(d.chat_url)window.open(d.chat_url,'_blank');
        else if(d.id)window.open(GW+'/?conv='+encodeURIComponent(d.id),'_blank');
        togglePanel(false);
      }).catch(function(e){
        bodyEl.textContent='Chat failed: '+(e.message||e);
        bodyEl.classList.add('loading');
      }).finally(function(){setBusy(false);});
    };
    function runGrokWeb(){
      state.pageData=extractPage();
      if(!state.pageData.text){
        showResults();
        bodyEl.textContent='No readable text on this page.';
        bodyEl.classList.remove('loading');
        return;
      }
      setBusy(true);
      showResults();
      bodyEl.textContent='Opening Grok Web…';
      bodyEl.classList.add('loading');
      fetch(GW+'/api/page/grok-web',{
        method:'POST',
        headers:{'Content-Type':'application/json'},
        body:JSON.stringify(state.pageData)
      }).then(function(r){return r.json();}).then(function(d){
        if(d.error)throw new Error(d.error);
        if(d.grok_url)window.open(d.grok_url,'_blank');
        else throw new Error('missing grok_url');
        togglePanel(false);
      }).catch(function(e){
        bodyEl.textContent='Grok Web failed: '+(e.message||e);
        bodyEl.classList.remove('loading');
      }).finally(function(){setBusy(false);});
    }
    function runSummarize(){
      state.pageData=extractPage();
      if(!state.pageData.text){
        showResults();
        bodyEl.textContent='No readable text on this page.';
        bodyEl.classList.remove('loading');
        return;
      }
      state.summary='';
      setBusy(true);
      showResults();
      bodyEl.textContent='Grok is summarizing…';
      bodyEl.classList.add('loading');
      copyBtn.disabled=true;
      chatBtn.disabled=true;
      fetch(GW+'/api/page/summarize/stream',{
        method:'POST',
        headers:{'Content-Type':'application/json'},
        body:JSON.stringify(state.pageData)
      }).then(function(res){
        if(!res.ok)return res.json().then(function(e){throw new Error(e.error||res.statusText);});
        var reader=res.body.getReader();
        var decoder=new TextDecoder();
        var buf='',text='';
        function pump(){
          return reader.read().then(function(chunk){
            if(chunk.done){
              state.summary=text.trim()||'No summary returned.';
              bodyEl.textContent=state.summary;
              bodyEl.classList.remove('loading');
              setBusy(false);
              copyBtn.disabled=false;
              chatBtn.disabled=false;
              return;
            }
            buf+=decoder.decode(chunk.value,{stream:true});
            var idx;
            while((idx=buf.indexOf('\n'))>=0){
              var line=buf.slice(0,idx).trim();
              buf=buf.slice(idx+1);
              if(!line||line[0]!=='{')continue;
              try{
                var evt=JSON.parse(line);
                if(evt.type==='text'&&evt.data){
                  text+=evt.data;
                  bodyEl.textContent=text;
                  bodyEl.classList.remove('loading');
                }else if(evt.type==='result'&&evt.text){
                  text=evt.text;
                }else if(evt.type==='error'){
                  throw new Error(evt.error||'summarize failed');
                }
              }catch(parseErr){}
            }
            return pump();
          });
        }
        return pump();
      }).catch(function(e){
        bodyEl.textContent='Error: '+(e.message||e);
        bodyEl.classList.add('loading');
        setBusy(false);
      });
    }
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