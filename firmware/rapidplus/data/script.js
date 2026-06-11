window.addEventListener("load", getReadings);

var chartT = new Highcharts.Chart({
  chart: {
    renderTo: "chart",
  },
  series: [
    {
      name: "#1",
      type: "line",
      color: "#00BFFF",
      marker: {
        symbol: "circle",
        radius: 3,
        fillColor: "#00BFFF",
      },
    },
    {
      name: "#2",
      type: "line",
      color: "#FF0000",
      marker: {
        symbol: "circle",
        radius: 3,
        fillColor: "#FF0000",
      },
    },
    {
      name: "#3",
      type: "line",
      color: "#FFFF00",
      marker: {
        symbol: "circle",
        radius: 3,
        fillColor: "#FFFF00",
      },
    },
    {
      name: "#4",
      type: "line",
      color: "#32CD32",
      marker: {
        symbol: "circle",
        radius: 3,
        fillColor: "#32CD32",
      },
    },
    {
      name: "#5",
      type: "line",
      color: "#D2691E",
      marker: {
        symbol: "circle",
        radius: 3,
        fillColor: "#D2691E",
      },
    },
    {
      name: "#6",
      type: "line",
      color: "#00FFFF",
      marker: {
        symbol: "circle",
        radius: 3,
        fillColor: "#00FFFF",
      },
    },
    {
      name: "#7",
      type: "line",
      color: "#9400D3",
      marker: {
        symbol: "circle",
        radius: 3,
        fillColor: "#9400D3",
      },
    },
    {
      name: "#8",
      type: "line",
      color: "#9ACD32",
      marker: {
        symbol: "circle",
        radius: 3,
        fillColor: "#9ACD32",
      },
    },
    {
      name: "#9",
      type: "line",
      color: "#0000FF",
      marker: {
        symbol: "circle",
        radius: 3,
        fillColor: "#0000FF",
      },
    },
    {
      name: "#10",
      type: "line",
      color: "#FF69B4",
      marker: {
        symbol: "circle",
        radius: 3,
        fillColor: "#FF69B4",
      },
    },
  ],
  title: {
    text: undefined,
  },
  xAxis: {
    title: {
      text: "Time (min)",
    },
    tickInterval: 1,
  },
  yAxis: {
    title: {
      text: "Fluorescent (nm FAM)",
    },
    tickInterval: 50,
  },
  credits: {
    enabled: false,
  },
});

function plotResult(jsonValue) {
  var keys = Object.keys(jsonValue);
  console.log(keys);
  console.log(keys.length);

  for (var i = 0; i < keys.length; i++) {
    var x = new Date().getTime();
    console.log(x);
    const key = keys[i];
    var y = Number(jsonValue[key]);
    console.log(y);

    if (chartT.series[i].data.length > 40) {
      chartT.series[i].addPoint([x, y], true, true, true);
    } else {
      chartT.series[i].addPoint([x, y], true, false, true);
    }
  }
}

function getReadings() {
  var xhr = new XMLHttpRequest();
  xhr.onreadystatechange = function () {
    if (this.readyState == 4 && this.status == 200) {
      var myObj = JSON.parse(this.responseText);
      console.log(myObj);
      plotResult(myObj);
    }
  };
  xhr.open("GET", "/readings", true);
  xhr.send();
}

if (!!window.EventSource) {
  var source = new EventSource("/events");

  source.addEventListener(
    "open",
    function (e) {
      console.log("Events Connected");
    },
    false
  );

  source.addEventListener(
    "error",
    function (e) {
      if (e.target.readyState != EventSource.OPEN) {
        console.log("Events Disconnected");
      }
    },
    false
  );

  source.addEventListener(
    "message",
    function (e) {
      console.log("message", e.data);
    },
    false
  );

  source.addEventListener(
    "new_readings",
    function (e) {
      console.log("new_readings", e.data);
      var myObj = JSON.parse(e.data);
      console.log(myObj);
      plotResult(myObj);
    },
    false
  );
}
