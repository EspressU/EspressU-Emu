(function(){
  var root=document.documentElement;
  function get(k){try{return localStorage.getItem(k)}catch(e){return null}}
  function set(k,v){try{localStorage.setItem(k,v)}catch(e){}}
  var saved=get('espressu-theme'); if(saved) root.setAttribute('data-theme',saved);
  var themeBtn=document.getElementById('theme');
  if(themeBtn) themeBtn.addEventListener('click',function(){
    var dark=root.getAttribute('data-theme')==='dark'||(!root.getAttribute('data-theme')&&!matchMedia('(prefers-color-scheme: light)').matches);
    var next=dark?'light':'dark'; root.setAttribute('data-theme',next); set('espressu-theme',next);
  });
  var nav=document.getElementById('nav'), burger=document.getElementById('burger');
  if(burger){
    burger.addEventListener('click',function(){var o=nav.classList.toggle('open');burger.setAttribute('aria-expanded',o?'true':'false')});
    nav.addEventListener('click',function(e){if(e.target.tagName==='A'){nav.classList.remove('open');burger.setAttribute('aria-expanded','false')}});
  }
  var bar=document.getElementById('progress'), topBtn=document.getElementById('totop');
  var toc=[].slice.call(document.querySelectorAll('.toc a')), tsecs=toc.map(function(a){return document.querySelector(a.getAttribute('href'))});
  function onScroll(){
    var h=document.documentElement, max=h.scrollHeight-h.clientHeight;
    if(bar) bar.style.width=(max>0?(h.scrollTop/max*100):0)+'%';
    if(topBtn) topBtn.classList.toggle('show',h.scrollTop>700);
    if(toc.length){var y=h.scrollTop+140,cur=-1;tsecs.forEach(function(s,i){if(s&&s.offsetTop<=y)cur=i});toc.forEach(function(a,i){if(i===cur)a.setAttribute('aria-current','true');else a.removeAttribute('aria-current')})}
  }
  addEventListener('scroll',onScroll,{passive:true}); onScroll();
  var tt; function toast(m){var t=document.getElementById('toast');if(!t)return;t.textContent=m;t.classList.add('show');clearTimeout(tt);tt=setTimeout(function(){t.classList.remove('show')},2400)}

  // setup checklist, remembered in this browser
  var boxes=[].slice.call(document.querySelectorAll('#steps .chk'));
  if(boxes.length){
    var state=[]; try{state=JSON.parse(get('espressu-steps')||'[]')}catch(e){}
    var paint=function(first){
      var n=0; boxes.forEach(function(b,i){var done=!!state[i]; b.checked=done; b.closest('li').classList.toggle('done',done); if(done)n++;});
      document.querySelectorAll('[data-meter-text]').forEach(function(e){e.textContent=n+' of '+boxes.length+' done'});
      document.querySelectorAll('[data-meter-fill]').forEach(function(e){e.style.width=(n/boxes.length*100)+'%'});
      if(!first&&n===boxes.length)toast('All set. Enjoy your games.');
    };
    boxes.forEach(function(b,i){b.addEventListener('change',function(){state[i]=b.checked;set('espressu-steps',JSON.stringify(state));paint()})});
    var rs=document.getElementById('reset'); if(rs) rs.addEventListener('click',function(){state=[];set('espressu-steps','[]');paint(true)});
    paint(true);
  }

  // FAQ search
  var q=document.getElementById('faq-search');
  if(q){
    var items=[].slice.call(document.querySelectorAll('#faq-list details')), empty=document.getElementById('faq-empty');
    q.addEventListener('input',function(){
      var v=q.value.trim().toLowerCase(), n=0;
      items.forEach(function(d){var m=!v||d.textContent.toLowerCase().indexOf(v)>-1;d.style.display=m?'':'none';if(m)n++;if(v&&m)d.open=true;});
      empty.style.display=n?'none':'block';
    });
  }
})();
