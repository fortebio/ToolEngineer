#ifndef _INDEX_H
#define _INDEX_H

#include "define.h"

const char* index_html = R"rawliteral(

<!DOCTYPE html>
<html>
  <head>
    <title>ESP WEBSERVER</title>
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <link rel="icon" type="image/png" href="downlad.png">
    <link rel="stylesheet" type="text/css" href="style.css">
    <script src="https://code.highcharts.com/highcharts.js"></script>
    <script src="https://cdnjs.cloudflare.com/ajax/libs/jspdf-autotable/3.5.4/jspdf.plugin.autotable.min.js"></script>
    <script src="https://cdnjs.cloudflare.com/ajax/libs/jspdf/2.5.1/jspdf.umd.min.js"></script>
    <script src="https://cdnjs.cloudflare.com/ajax/libs/html2canvas/0.5.0-beta4/html2canvas.min.js"></script>
    <script src="https://code.highcharts.com/modules/accessibility.js"></script>
    <style>
      html {
        font-family: Arial, Helvetica, sans-serif;
        display: inline-block;
        text-align: center;
      }
      h1 {
        font-size: 1.8rem;
        color: white;
      }
      p {
        font-size: 1.4rem;
      }
      .topnav {
        overflow: hidden;
        background-color: #0A1128;
      }
	  h2 {
		text-align: center; 
		font-size: 1.2rem;
        font-weight: bold;
        color: #034078;
	  }
	  table {
        width: 80%;
        border-collapse: collapse;
        margin: 20px auto;
      }
      th, td {
        border: 1px solid #ddd;
        padding: 8px;
        text-align: center;
      }
      th {
        background-color: #f2f2f2;
      }
      body {
        margin: 0;
      }
      .content {
        padding: 5%;
      }
      .card-grid {
        max-width: 1200px;
        margin: 0 auto;
        display: grid;
        grid-gap: 2rem;
        grid-template-columns: repeat(auto-fit, minmax(200px, 1fr));
      }
      .card {
        background-color: white;
        box-shadow: 2px 2px 12px 1px rgba(140,140,140,.5);
      }
      .card-title {
        font-size: 1.2rem;
        font-weight: bold;
        color: #034078;
      }
      .idDevice {
        font-size: 0.8rem;
        font-weight: bold;
      }
      .time {
        font-size: 0.8rem;
        font-weight: bold;
      }
      .chart-container {
        padding-right: 5%;
        padding-left: 5%;
      }
      #getDataBtn {
            margin-top: 20px;
            background-color: #007BFF; 
            color: white; 
            border: none; 
            padding: 10px 20px; 
            font-size: 1rem;
            border-radius: 5px; 
            cursor: pointer;
            transition: background-color 0.3s, transform 0.2s; 
        }
      #getDataBtn:hover {
            background-color: #0056b3; 
            transform: scale(1.05);
        }
      #reloadBtn {
            margin-top: 20px;
            background-color: #007BFF; 
            color: white; 
            border: none; 
            padding: 10px 20px; 
            font-size: 1rem;
            border-radius: 5px; 
            cursor: pointer;
            transition: background-color 0.3s, transform 0.2s; 
        }
      #reloadBtn:hover {
            background-color: #0056b3; 
            transform: scale(1.05);
        }
    </style>
  </head>
  <body>
    <div class="topnav">
      <h1>RAPID PLUS</h1>
    </div>
    <div class="content" id="content">
      <div class="card-grid">
        <div class="card">
		      <button id="getDataBtn">Get Data</button>
          <button id="reloadBtn">Clear Data</button>
          <p class="idDevice" id="Device">Device ID:</p>
          <p class="time" id="dateOfMeasurement">Date of measurement:</p>
		  <h2>PCR Result Table</h2>
			<table id="table">
				<thead>
					<tr>
						<th>Slot</th>
						<th>Amplification time</th>
						<th>Results</th>
					</tr>
				</thead>
				<tbody>
					<tr>
						<td>1</td>
						<td id="ct1"></td>
                        <td id="result1"></td>
					</tr>
					<tr>
						<td>2</td>
						<td id="ct2"></td>
                        <td id="result2"></td>
					</tr>
					<tr>
						<td>3</td>
						<td id="ct3"></td>
                        <td id="result3"></td>
					</tr>
					<tr>
						<td>4</td>
						<td id="ct4"></td>
                        <td id="result4"></td>
					</tr>
					<tr>
						<td>5</td>
						<td id="ct5"></td>
                        <td id="result5"></td>
					</tr>
					<tr>
						<td>6</td>
						<td id="ct6"></td>
                        <td id="result6"></td>
					</tr>
					<tr>
						<td>7</td>
						<td id="ct7"></td>
                        <td id="result7"></td>
					</tr>
					<tr>
						<td>8</td>
						<td id="ct8"></td>
                        <td id="result8"></td>
					</tr>
					<tr>
						<td>9</td>
						<td id="ct9"></td>
                        <td id="result9"></td>
					</tr>
					<tr>
						<td>10</td>
						<td id="ct10"></td>
                        <td id="result10"></td>
					</tr>
				</tbody>
			</table>
          <p class="card-title">Realtime LAMP PCR Amplification plot</p>
          <div id="chart" class="chart-container"></div>
        </div>
      </div>
    </div>
    <script>
      Highcharts.setOptions({
			time: {
                useUTC: false
			}
	  });
      var chartT = new Highcharts.Chart({
        chart: {
          renderTo: 'chart'
        },
        series: [
          { name: '#1',
            type: 'line',
            color: '#00BFFF',
            marker: { enabled: false }, 
            dataLabels: { enabled: false } 
          },
          { name: '#2', 
            type: 'line', 
            color: '#FF0000', 
            marker: { enabled: false },
            dataLabels: { enabled: false } 
          },
          { name: '#3',
            type: 'line', 
            color: '#FFFF00', 
            marker: { enabled: false },
            dataLabels: { enabled: false } 
          },
          { name: '#4',
            type: 'line', 
            color: '#32CD32', 
            marker: { enabled: false },
            dataLabels: { enabled: false } 
          },
          { name: '#5',
            type: 'line', 
            color: '#D2691E', 
            marker: { enabled: false },
            dataLabels: { enabled: false } 
          },
          { name: '#6',
            type: 'line', 
            color: '#00FFFF', 
            marker: { enabled: false },
            dataLabels: { enabled: false } 
          },
          { name: '#7',
            type: 'line', 
            color: '#9400D3', 
            marker: { enabled: false },
            dataLabels: { enabled: false } 
          },
          { name: '#8',
            type: 'line', 
            color: '#9ACD32', 
            marker: { enabled: false },
            dataLabels: { enabled: false } 
          },
          { name: '#9',
            type: 'line', 
            color: '#0000FF', 
            marker: { enabled: false },
            dataLabels: { enabled: false } 
          },
          { name: '#10',
            type: 'line', 
            color: '#FF69B4', 
            marker: { enabled: false },
            dataLabels: { enabled: false } 
          }
        ],
        title: { text: undefined },
        xAxis: {
          plotLines: [{
            value: 0,
            color: 'black',
            width: 2,
            zIndex: 5 }], 
          title: { text: 'Time (minutes)' },
          labels: {
            style: {
                fontSize: '14px'
            }
          }
        },
        yAxis: {
          plotLines: [{
            value: 0,
            color: 'black',
            width: 2,
            zIndex: 5 }],
          title: { text: 'Fluorescent (nm FAM)' },
        },
        credits: { enabled: false }
      });

      function plotResult(jsonValue) {

        var keys = Object.keys(jsonValue);
        var totalPoints = 120;
        var totalMinutes = 40;

        for (var i = 0; i < keys.length; i++) {
          var key = keys[i];
          var dataArray = jsonValue[key];

          for (var j = 0; j < totalPoints; j++) {
            
            var y = Number(dataArray[j]);
            var x = (j / (totalPoints - 1)) * totalMinutes;

            if (i < chartT.series.length) {
              chartT.series[i].addPoint([x, y], true, false, true);
            }  
          }
        }  
      }

      function updateDateOfMeasurement() {
            var now = new Date();
            var options = { year: 'numeric', month: '2-digit', day: '2-digit'};
            document.getElementById('dateOfMeasurement').innerText = "Date of measurement: " + now.toLocaleString('vi-VN', options);
      }

      function plotCT(jsonValue) {
        var keys = Object.keys(jsonValue);
        for (var i = 0; i < keys.length; i++) {
                document.getElementById(`ct${i + 1}`).innerText = jsonValue[keys[i]];
            }
      }

      function plotRe(jsonValue) {
        var keys = Object.keys(jsonValue);
        for (var i = 0; i < keys.length; i++) {
          var valueStr = jsonValue[keys[i]];
          var resultElement = document.getElementById(`result${i + 1}`);

          resultElement.innerText = valueStr;

          if (valueStr === "Positive") {
              resultElement.style.color = 'red';
          } 
          else if (valueStr === "Slide Positive") {
              resultElement.style.color = 'yellow';
          }
          else {
              resultElement.style.color = 'black';
          }
        }
      }

      document.getElementById('reloadBtn').onclick = function() {
        location.reload();
      };

      document.getElementById('getDataBtn').onclick = function() {
        var xhr = new XMLHttpRequest();
        xhr.open("GET", "/getdata", true);
        xhr.onreadystatechange = function() {
          if (xhr.readyState == 4 && xhr.status == 200) {
            var myObj = JSON.parse(xhr.responseText);
            var id_device = myObj.id_device;
            var CT_value = myObj.CT_value;
            var result = myObj.result;

            delete myObj.id_device;
            delete myObj.CT_value;
            delete myObj.result;

            document.getElementById('Device').innerText = "Device ID: " + id_device;
            plotCT(CT_value);
            plotRe(result);
            plotResult(myObj);
          }
        };
        xhr.send();
        updateDateOfMeasurement();
      };
    </script>
  </body>
</html>
)rawliteral";

#endif
