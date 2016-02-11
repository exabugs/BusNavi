var request = require("request");
var cheerio = require("cheerio");

exports.handler = function (event, context) {

  var options = {
    uri: 'http://tokyu.bus-location.jp/blsys/navis',
    form: {
      VID: "rsc",
      EID: "rd",
      SCT: 2,
      DSMK: Number(event.DSMK),
      ASMK: Number(event.ASMK)
    }
  };

  function trim(node) {
    return node.text().replace(/[\r\n\t]/g, "");
  }

  request.post(options,
    function (error, response, body) {
      if (event.test) {
        body = require('fs').readFileSync('check.html', 'utf-8');
      }
      var $ = cheerio.load(body);
      var pos = [];
      $(".approach tbody tr").each(function () {
        var tr = $(this);
        if (tr.hasClass("stopline")) {
          pos.push([]);
        }
        if (tr.hasClass("busline") && pos.length) {
          tr.children(".colmsg").each(function () {
            $(this).children("div").each(function () {
              var waittm = $(this).children(".waittm").remove();
              var item = {
                route: trim($(this)),
                wait: parseInt(trim(waittm))
              };
              pos[pos.length - 1].push(item);
            });
          });
        }
      });
      pos.shift(); // 先頭 (arrstopline) は不要
      //console.log(pos);
      var now = new Date();
      context.done(null, {
        time: ~~(now.getTime() / 1000),
        DSMK: Number(event.DSMK),
        ASMK: Number(event.ASMK),
        item: pos
      });
    }
  );
};
