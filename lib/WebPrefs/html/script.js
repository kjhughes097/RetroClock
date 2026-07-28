function uploadFile(target) {
  loading(true);
  var fileInput = document.getElementById('firmware');
  var status = document.getElementById('filename');
  if (fileInput.files.length === 0 || fileInput.files[0].size === 0) {
    status.value = "Select a file";
    loading(false);
    return;
  }
  var formData = new FormData();
  formData.append('target', target);
  formData.append('firmware', fileInput.files[0]);
  var xhr = new XMLHttpRequest();
  xhr.open('POST', '/update', true);
  xhr.upload.onprogress = function (e) {
    if (e.lengthComputable) {
      var percentComplete = (e.loaded / e.total) * 100;
      status.value = 'Progress: ' + percentComplete.toFixed(2);
    }
  };
  xhr.onload = function () {
    loading(false);
    status.value = xhr.responseText;
    if (xhr.status !== 200)
      fileInput.value = "";
    else
      window.location.href = "./done";
  };
  xhr.onerror = function () {
    loading(false);
    status.value = "Error occurred during upload.";
  };
  xhr.send(formData);
}

function init(fnc) {
  var f = document.getElementById('myPrefs');
  if (f) {
    f.onsubmit = postForm;
    initEventListeners();
  }

  getJSON('./getJson?fnc=' + fnc, function (err, response) {
    if (err) {
      alert("data load error. try to reload!");
    } else {
      setValuesByIDs(response);
    }
  });
}

function initEventListeners() {
  var cbs = document.querySelectorAll('input[type="checkbox"], input[type="text"], input[type="password"], input[type="number"], input[type="range"]');
  for (var i = 0; i < cbs.length; i++) {
    console.log((i + 2) + " : " + cbs[i].id);
    if (cbs[i].type === "checkbox") {
      cbs[i].addEventListener('click', function () {
        if (this.hasAttribute("data-uppost") || this.hasAttribute("data-up")) {
          updatePost(this, this.hasAttribute("data-uppost"), true);
        }
      });
    } else {
      cbs[i].addEventListener('change', function () {
        var target;
        if (this.type === "range") {
          target = document.getElementById(this.id.replace(/_range$/, ""));
          if (target) {
            target.value = this.value;
            target.dispatchEvent(new Event("change"));
          }
        } else if (this.type === "number") {
          target = document.getElementById(this.id + "_range");
          if (target) {
            target.value = this.value;
          }
        }

        if (this.hasAttribute("data-uppost") || this.hasAttribute("data-up")) {
          updatePost(this, this.hasAttribute("data-uppost"), true);
        }
      });
      cbs[i].addEventListener('keydown', function (e) {
        if (e.key === 'Enter' || e.keyCode === 13) {
          e.preventDefault();
          this.blur();
        }
      });
    }
  }
}

function setValuesByNames(names) {
  for (var key in names) {
    var elem = document.getElementsByName(key)[0];
    elem.value = decodeURIComponent(names[key]);
    if (elem.type == 'checkbox') {
      elem.checked = (names[key] == 'on');
      if (names[key] == 'off')
        elem.previousElementSibling.name = elem.name;
    }
  }
}

function setValuesByIDs(ids) {
  for (var key in ids) {
    var elem = document.getElementById(key);
    if (!elem) continue;
    var val = decodeURIComponent(ids[key]);
    if (elem.type == 'checkbox') {
      elem.value = val;
      elem.checked = (ids[key] == 'on');
      if (ids[key] == 'off')
        elem.previousElementSibling.name = elem.id;
      else
        elem.name = elem.id;
      applyRuleset(elem, elem.getAttribute('activation-rules'), false);
    } else if(elem.tagName == "textarea") {
      elem.innerHTML = val;
      elem.innerText = val; //Firefox
      elem.name = elem.id;
    } else {
      elem.value = val;
      elem.name = elem.id;
      if(elem.type == 'number') {
        var target = document.getElementById(elem.id + "_range");
        if(target) {
          target.value = val;
        }
      }
    }
  }
}

function getJSON(url, callback) {
  var xhr = new XMLHttpRequest();
  loading(true);
  xhr.open('GET', url, true);
  xhr.responseType = 'json';
  xhr.onload = function () {
    loading(false);
    if (xhr.status === 200) {
      callback(null, xhr.response);
    } else {
      callback(xhr.status);
    }
  };
  xhr.send();
}

function loading(toggle) {
  var buttons = document.getElementsByTagName("button");
  for (var i = 0; i < buttons.length; i++) {
    buttons[i].disabled = toggle;
  }
  if (toggle) {
    spinnerOn();
  } else {
    spinnerOff();
  }
}

function applyRuleset(checkbox, ruleset, toggle) {
  var cbs = document.querySelectorAll('input[type="checkbox"], input[type="text"], input[type="password"], textarea');

  if (ruleset) {
    var rs = JSON.parse(ruleset);
    for (var t = 0; t < rs.length; t++) {
      var i = rs[t];
      if (!checkbox.checked) i = -(i);
      var index = Math.abs(i) - 1;
      if (!cbs[index]) {
        console.warn('Index ' + index + ' out of range');
        return;
      }

      var element = cbs[index];

      if (element.type === 'checkbox' && toggle && i < 0 && element.checked == true) {
        updatePost(element, true, false);
        element.checked = false;
        applyRuleset(element, element.getAttribute('activation-rules'), false);
      }

      if (!toggle && (element.type === 'text' || element.type === 'password' || element.type === 'checkbox')) {
        if (element.type === 'checkbox') {
          if (i < 0) {
            element.parentElement.classList.add("disabled");
          } else {
            element.parentElement.classList.remove("disabled");
          }
        }
        element.disabled = (i < 0);
      }
    }
  }
}

/*
function update(checkbox, post) {
  console.log("chkbox status:" + checkbox.checked + "\n");
  applyRuleset(checkbox, checkbox.getAttribute('activation-rules'), false);
  applyRuleset(checkbox, checkbox.getAttribute('toggle-rules'), true);
  postUpdate(checkbox, post);
}
*/

function updatePost(element, post, check_rules) {
  if (element.type == 'checkbox') {    
    if(check_rules) {
      console.log("chkbox status:" + element.checked + "\n");
      applyRuleset(element, element.getAttribute('activation-rules'), false);
      applyRuleset(element, element.getAttribute('toggle-rules'), true);
    }

    if (element.value == 'off') {
      element.value = 'on';
      element.previousElementSibling.name = '';
      element.name = element.id;
    } else {
      element.value = 'off';
      element.previousElementSibling.name = element.id;
      element.name = '';
    }
  }

  if (post == false) {
    showBanner("Save & Reboot to apply", 2000);
  }else {
    var form = document.getElementById('myPrefs');
    var url = form.getAttribute('action');
    var params = encodeURIComponent(element.id) + "=" + encodeURIComponent(element.value);

    var xhr = new XMLHttpRequest();
    xhr.open('POST', url, true);
    xhr.setRequestHeader('Content-Type', 'application/x-www-form-urlencoded');
    xhr.onload = function () {
      if (xhr.status !== 200) {
        console.error('Error instantPost:', xhr.status);
      }
    };
    xhr.send(params);
  }
}

function postForm(event) {
  event.preventDefault();
  var xhr = new XMLHttpRequest();
  loading(true);
  xhr.open('POST', event.target.action);
  xhr.onload = function () {
    loading(false);
    var buttons = document.getElementsByTagName("button");
    for (var i = 0; i < buttons.length; i++) {
      buttons[i].disabled = false;
    }
    if (xhr.status === 200) {
      console.log("Done!");
    } else {
      console.log("Form load error!");
    }
  };
  xhr.send(new FormData(event.target));
}

function postJSON(url, data, callback) {
  xhr = new XMLHttpRequest();
  xhr.open('POST', url, true);
  xhr.setRequestHeader('Content-Type', 'application/json');
  xhr.onload = function () {
    if (xhr.status === 200) {
      var response = JSON.parse(xhr.responseText);
      callback(null, response);
    } else {
      callback(xhr.status);
    }
  };
  xhr.send(JSON.stringify(data));
}

function spinnerOn() {
  var sp = document.getElementById('spn');
  if (sp) {
    sp.classList.add('spinner');
    sp.style.display = "block";
  }
}

function spinnerOff() {
  var sp = document.getElementById('spn');
  if (sp) {
    sp.style.display = "none";
    sp.classList.remove('spinner');
  }
}

function showBanner(message, duration) {
  var banner = document.getElementById('banner');
  banner.textContent = message;
  banner.classList.add('show');

  setTimeout(function () {
    banner.classList.remove('show');
  }, duration);
}