var sheet_id = "1-lZEYILfJUsZD93jRTkceEC-7fv8TIEzrE5mWjXO3vo"


function doPost(e) {
  try {
    var data = JSON.parse(e.postData.contents);
    if (data.method === "append") {

      sheetData(data); // Ghi dữ liệu vào sheet Data
      sheetRulst(data); // Ghi dữ liệu vào sheet Result
    }

    return ContentService.createTextOutput("Data received").setMimeType(ContentService.MimeType.TEXT);

  } catch (err) {
    return ContentService.createTextOutput("Error: " + err).setMimeType(ContentService.MimeType.TEXT);
  }
}

function sheetData(data) {
  var sheet_name_data = SpreadsheetApp.openById(sheet_id).getSheetByName("Data");
  var lastRow_sheetData = sheet_name_data.getLastRow();
  var startRow_sheetData = lastRow_sheetData + 2;

  sheet_name_data.getRange(startRow_sheetData, 13).setValue("ID device"); // Cột M
  sheet_name_data.getRange(startRow_sheetData, 14).setValue("Version"); // Cột N
  sheet_name_data.getRange(startRow_sheetData, 15).setValue("Timestamp (GMT +7)"); // Cột O
  sheet_name_data.getRange(startRow_sheetData + 1, 13).setValue(data.id_device || "N/A"); // Cột M
  sheet_name_data.getRange(startRow_sheetData + 1, 14).setValue(data.version || "Unknown"); // Cột N
  sheet_name_data.getRange(startRow_sheetData + 1, 15).setValue(data.time || "N/A"); // Cột O

  // Ghi Slopes, Origin, LED power
  sheet_name_data.getRange(startRow_sheetData, 1).setValue("Slopes");
  sheet_name_data.getRange(startRow_sheetData, 2, 1, data.slopes.length).setValues([data.slopes]);
  sheet_name_data.getRange(startRow_sheetData + 1, 1).setValue("Origin");
  sheet_name_data.getRange(startRow_sheetData + 1, 2, 1, data.origins.length).setValues([data.origins]);
  sheet_name_data.getRange(startRow_sheetData + 2, 1).setValue("LED Power");
  sheet_name_data.getRange(startRow_sheetData + 2, 2, 1, data.LED_power.length).setValues([data.LED_power]);
  // Ghi amplification time
  sheet_name_data.getRange(startRow_sheetData + 4, 1).setValue("Amplification Time");
  sheet_name_data.getRange(startRow_sheetData + 4, 2).setValue(data.amplification[0]["Slot 1"].length || 0); // Số vòng lặp

  // Ghi dữ liệu amplification
  if (data.amplification && data.amplification.length > 0) {
    var amplification = data.amplification[0];
    var slotNames = Object.keys(amplification);
    var loopCount = amplification[slotNames[0]].length;

    var resultMatrix = [];
    for (var i = 0; i < loopCount; i++) {
      var row = [i + 1]; // Cột A: Loop index
      for (var j = 0; j < slotNames.length; j++) {
        row.push(amplification[slotNames[j]][i] || "");
      }
      resultMatrix.push(row);
    }

    // Ghi tiêu đề: Loop + Slot names
    var header = ["Time"].concat(slotNames);
    sheet_name_data.getRange(startRow_sheetData + 6, 1, 1, header.length).setValues([header]);

    // Ghi dữ liệu loop từ hàng tiếp theo
    sheet_name_data.getRange(startRow_sheetData + 7, 1, resultMatrix.length, header.length).setValues(resultMatrix);
  }
}

function sheetRulst(data) {
  var sheet_name_result = SpreadsheetApp.openById(sheet_id).getSheetByName("Result");
  var lastRow_sheetResult = sheet_name_result.getLastRow();
  var startRow_sheetResult = lastRow_sheetResult + 1;

  // Ghi thông tin ID, version, time vào các ô cột M, N, O tương ứng
  // sheet_name_result.getRange(startRow_sheetResult, 13).setValue("ID device"); // Cột M
  // sheet_name_result.getRange(startRow_sheetResult, 14).setValue("Version"); // Cột N
  // sheet_name_result.getRange(startRow_sheetResult, 15).setValue("Timestamp (GMT +7)"); // Cột O
  sheet_name_result.getRange(startRow_sheetResult, 13).setValue(data.id_device || "N/A"); // Cột M
  sheet_name_result.getRange(startRow_sheetResult, 14).setValue(data.version || "Unknown"); // Cột N
  sheet_name_result.getRange(startRow_sheetResult, 15).setValue(data.time || "N/A"); // Cột O

  // Ghi CT Values và Results
  sheet_name_result.getRange(startRow_sheetResult, 1).setValue("CT Values");
  sheet_name_result.getRange(startRow_sheetResult, 2, 1, data.CT_value.length).setValues([data.CT_value]);

  sheet_name_result.getRange(startRow_sheetResult + 1, 1).setValue("Results");
  sheet_name_result.getRange(startRow_sheetResult + 1, 2, 1, data.result.length).setValues([data.result]);
}