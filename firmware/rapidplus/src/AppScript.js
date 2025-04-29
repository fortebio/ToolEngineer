var sheet_id = "1Glrc-5CLi9WzEZ4pBjXXR-6PV94JBFrnEi-b1djlxeU"
var folderId = "1nAQT5LBkFJZ1OXIVOHqSRL-ZhkzcWwgH"; // ID của thư mục trên Google Drive

// Main function to handle incoming POST requests
function doPost(e) {
  try {
    var data = JSON.parse(e.postData.contents);
    if (data.method === "append") {

      sheetRulst(data); // Ghi dữ liệu vào sheet Result
      sheetData(data); // Ghi dữ liệu vào sheet Data
    }
    return ContentService.createTextOutput("Data received").setMimeType(ContentService.MimeType.TEXT);
  } catch (err) {
    return ContentService.createTextOutput("Error: " + err).setMimeType(ContentService.MimeType.TEXT);
  }
}

function sheetData(data) {
  var sheet_name_data = SpreadsheetApp.openById(sheet_id).getSheetByName("Data");
  var lastRow_sheetData = sheet_name_data.getLastRow();
  var startRow_sheetData = lastRow_sheetData + 1;

  sheet_name_data.getRange(startRow_sheetData, 12).setValue(data.id_device || "N/A"); // Cột M
  sheet_name_data.getRange(startRow_sheetData, 13).setValue(data.version || "Unknown"); // Cột N
  sheet_name_data.getRange(startRow_sheetData, 14).setValue(data.time || "N/A"); // Cột O

  // Ghi Slopes, Origin, LED power
  sheet_name_data.getRange(startRow_sheetData, 1).setValue("Slopes");
  sheet_name_data.getRange(startRow_sheetData, 2, 1, data.slopes.length).setValues([data.slopes]);
  sheet_name_data.getRange(startRow_sheetData + 1, 1).setValue("Origin");
  sheet_name_data.getRange(startRow_sheetData + 1, 2, 1, data.origins.length).setValues([data.origins]);
  sheet_name_data.getRange(startRow_sheetData + 2, 1).setValue("LED Power");
  sheet_name_data.getRange(startRow_sheetData + 2, 2, 1, data.LED_power.length).setValues([data.LED_power]);
  // Ghi amplification time
  sheet_name_data.getRange(startRow_sheetData + 3, 1).setValue("Amplification");
  sheet_name_data.getRange(startRow_sheetData + 3, 2, 1, data.amplification.length).setValues([data.amplification]);
}

function sheetRulst(data) {
  var sheet_name_result = SpreadsheetApp.openById(sheet_id).getSheetByName("Result");
  var lastRow_sheetResult = sheet_name_result.getLastRow();
  var startRow_sheetResult = lastRow_sheetResult + 1;

  sheet_name_result.getRange(startRow_sheetResult, 11).setValue(data.id_device || "N/A"); // Cột M
  sheet_name_result.getRange(startRow_sheetResult, 12).setValue(data.version || "Unknown"); // Cột N
  sheet_name_result.getRange(startRow_sheetResult, 13).setValue(data.time || "N/A"); // Cột O

  // Ghi CT Values và Results
  sheet_name_result.getRange(startRow_sheetResult, 1, 1, data.result.length).setValues([data.result]);
}

function saveAmplificationToFolder(data) {
  var folder = DriveApp.getFolderById(folderId);
  var fileName = "Log_" + (data.id_device) + "-" + (data.time || new Date().toISOString()) + ".txt";
  var file = folder.getFilesByName(fileName);

  // Biến lưu toàn bộ nội dung
  var content = "";

  content += "Device ID: " + data.id_device + "\n";
  content += "Version: " + data.version + "\n";
  content += "Time: " + data.time + "\n\n";

  content += "Slopes: " + data.slopes.join(", ") + "\n";
  content += "Origins: " + data.origins.join(", ") + "\n";
  content += "LED Power: " + data.LED_power.join(", ") + "\n";
  content += "CT Values: " + data.CT_value.join(", ") + "\n";
  content += "Result: " + data.result.join(" | ") + "\n\n";

  content += "Amplification:\n";
  data.amplification.forEach((line, index) => {
    content += `  Row ${index + 1}: ` + line + "\n";
  });

  if (file.hasNext()) {
    var existingFile = file.next();
    existingFile.setContent(content); // Ghi đè nội dung
    Logger.log("File already exists. Overwritten.");
  } else {
    folder.createFile(fileName, content, MimeType.PLAIN_TEXT);
    Logger.log("File created.");
  }
}

function test_saveAmplificationToFolder() {
  var testData = {
    method: "append",
    id_device: "RA_TEST",
    version: "V2.2.1-test",
    time: "23-04-2025 15:00:00",
    slopes: [1, 1, 1, 1, 1, 1, 1, 1, 1, 1],
    origins: [0, 0, 0, 0, 0, 0, 0, 0, 0, 0],
    LED_power: [120, 120, 120, 120, 120, 120, 120, 120, 120, 120],
    CT_value: [2, 4, 36, 2, 3, 33, 14, 35, 28, 35],
    result: [
      "2 | N", "4 | N", "36 | N", "2 | N", "3 | N", "33 | N", "14 | N", "35 | N", "28 | N", "35 | N"
    ],
    amplification: [
      "376,376,374,373,...",
      "384,381,379,376,...",
      "306,304,308,309,...",
      "372,368,368,370,...",
      "417,412,412,410,...",
      "312,315,312,314,...",
      "348,350,350,349,...",
      "302,298,300,299,...",
      "288,288,288,288,...",
      "383,383,383,381,..."
    ]
  };

  saveAmplificationToFolder(testData);
}
