var macdict = {
}

var wait = false;
function globalThrottle(callback, args) {
  if (!wait) {
    callback.apply(null, args);
    wait = true;
    setTimeout(function () {
      wait = false;
    }, 100);
  }
}

function sendLEDcommand(el) {
  while ((el = el.parentElement) && !el.classList.contains('object-form'));
  var mac = el.getAttribute('data-mac');
  $.get('http://'+macdict[mac]['ip']+'/led?' + $(el).serialize());
}

_.mixin({templateFromUrl: function (url, data, settings) {
    var templateHtml = "";
    this.cache = this.cache || {};

    if (this.cache[url]) {
        templateHtml = this.cache[url];
    } else {
        $.ajax({
            url: url,
            method: "GET",
            async: false,
            success: function(data) {
                templateHtml = data;
            }
        });

        this.cache[url] = templateHtml;
    }

    return _.template(templateHtml, data, settings);
}});

var object_template = _.templateFromUrl('static/object_template.html');

var objectsSource  = new EventSource('registered');     
objectsSource.addEventListener('message', function (event) {
  var macdict_new = JSON.parse(event.data); 
  for (const m in macdict_new) {
    if (macdict.hasOwnProperty(m)) {
    } else {
      $('#objects').append(object_template({mac:m,ip:macdict_new[m]['ip']}));
      Intercooler.processNodes($('#'+m));
    }
    $('#'+m).data('time', macdict_new[m]['t']);
  }
  macdict = macdict_new;
});
