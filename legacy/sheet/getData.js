var sheet_id = "1Glrc-5CLi9WzEZ4pBjXXR-6PV94JBFrnEi-b1djlxeU";
var folderId = "1nAQT5LBkFJZ1OXIVOHqSRL-ZhkzcWwgH"; // ID của thư mục trên Google Drive
var folderError = "1AMKNXae54j6o6ivcggsgQ3tYbzakscu7";
var folderLog = "1-9e8Kv-HbK20V81bqjx6JcFyN_Feqc6K";
function doPost(e) {
  try {
    var data = JSON.parse(e.postData.contents);

    if (data.method === "append") {
      sheetData(data); // Ghi dữ liệu vào sheet Data
      sheetResult(data); // Ghi dữ liệu vào sheet Result

      saveAmplificationToFolder(data); // (tô màu hàng Amplification mới đã làm trong sheetData)
      console.log("[Post] RPL");
    }

    if (data.method === "error") {
      sheetError(data);
      saveErrorToFolder(data);
      console.log("[Error] RPL");
    }

    return ContentService.createTextOutput("Data received").setMimeType(
      ContentService.MimeType.TEXT,
    );
  } catch (err) {
    return ContentService.createTextOutput("Error: " + err).setMimeType(
      ContentService.MimeType.TEXT,
    );
  }
}

function sheetData(data) {
  var sheet_name_data =
    SpreadsheetApp.openById(sheet_id).getSheetByName("Data");
  var lastRow_sheetData = sheet_name_data.getLastRow();
  var startRow_sheetData = lastRow_sheetData + 1;

  sheet_name_data
    .getRange(startRow_sheetData + 3, 12)
    .setValue(data.id_device || "N/A"); // Cột M
  sheet_name_data
    .getRange(startRow_sheetData + 3, 13)
    .setValue(data.version || "Unknown"); // Cột N
  sheet_name_data
    .getRange(startRow_sheetData + 3, 14)
    .setValue(new Date() || "N/A"); // Cột O
  sheet_name_data
    .getRange(startRow_sheetData + 3, 15)
    .setValue(data.kitId || "N/A"); // Cột O
  sheet_name_data
    .getRange(startRow_sheetData + 3, 16)
    .setValue(data.type_Upload || "N/A"); // Cột O

  // Ghi Slopes, Origin, LED power
  sheet_name_data.getRange(startRow_sheetData, 1).setValue("Slopes");
  sheet_name_data
    .getRange(startRow_sheetData, 2, 1, data.slopes.length)
    .setValues([data.slopes]);
  sheet_name_data.getRange(startRow_sheetData + 1, 1).setValue("Origin");
  sheet_name_data
    .getRange(startRow_sheetData + 1, 2, 1, data.origins.length)
    .setValues([data.origins]);
  sheet_name_data.getRange(startRow_sheetData + 2, 1).setValue("LED Power");
  sheet_name_data
    .getRange(startRow_sheetData + 2, 2, 1, data.LED_power.length)
    .setValues([data.LED_power]);
  // Ghi amplification time
  sheet_name_data.getRange(startRow_sheetData + 3, 1).setValue("Amplification");
  sheet_name_data
    .getRange(startRow_sheetData + 3, 2, 1, data.amplification.length)
    .setValues([data.amplification]);

  // Chỉ tô màu đúng hàng Amplification vừa thêm (không quét lại cả sheet mỗi POST)
  highlightAmplificationRow_(sheet_name_data, startRow_sheetData + 3);
}

function sheetResult(data) {
  var sheet_name_result =
    SpreadsheetApp.openById(sheet_id).getSheetByName("Result");
  var lastRow_sheetResult = sheet_name_result.getLastRow();
  var startRow_sheetResult = lastRow_sheetResult + 1;

  sheet_name_result
    .getRange(startRow_sheetResult, 11)
    .setValue(data.id_device || "N/A"); // Cột M
  sheet_name_result
    .getRange(startRow_sheetResult, 12)
    .setValue(data.version || "Unknown"); // Cột N
  sheet_name_result
    .getRange(startRow_sheetResult, 13)
    .setValue(new Date() || "N/A"); // Cột O
  sheet_name_result
    .getRange(startRow_sheetResult, 14)
    .setValue(data.kitId || "N/A");
  sheet_name_result
    .getRange(startRow_sheetResult, 15)
    .setValue(data.type_Upload || "N/A");
  // Ghi CT Values và Results
  sheet_name_result
    .getRange(startRow_sheetResult, 1, 1, data.result.length)
    .setValues([data.result]);
}

function sheetError(data) {
  if (data.method === "error") {
    var sheet_name_error =
      SpreadsheetApp.openById(sheet_id).getSheetByName("Error");
    const id_device = data.id_device || "N/A";
    const version = data.version || "Unknown";
    const timestamp = new Date() || "N/A";

    const rows = data.error.map((err) => [
      err.error_code,
      err.Slot,
      err.error_msg,
      version,
      id_device,
      timestamp,
    ]);

    sheet_name_error
      .getRange(
        sheet_name_error.getLastRow() + 1,
        1,
        rows.length,
        rows[0].length,
      )
      .setValues(rows);
  }
}

function saveAmplificationToFolder(data) {
  var folder = DriveApp.getFolderById(folderId);
  var fileName =
    "Log_" + data.id_device + "-" + new Date().toISOString() + ".txt";
  var file = folder.getFilesByName(fileName);

  var jsonString = JSON.stringify(data, null, 2);

  if (file.hasNext()) {
    var existingFile = file.next();
    existingFile.setContent(jsonString); // Ghi đè nội dung
    Logger.log("File already exists. Overwritten.");
  } else {
    folder.createFile(fileName, jsonString, MimeType.PLAIN_TEXT);
    Logger.log("File created.");
  }
}

function saveErrorToFolder(data) {
  var folder = DriveApp.getFolderById(folderError);
  var fileName =
    "Error_" + data.id_device + "-" + new Date().toISOString() + ".txt";
  var file = folder.getFilesByName(fileName);

  var jsonString = JSON.stringify(data, null, 2);

  if (file.hasNext()) {
    var existingFile = file.next();
    existingFile.setContent(jsonString); // Ghi đè nội dung
    Logger.log("File already exists. Overwritten.");
  } else {
    folder.createFile(fileName, jsonString, MimeType.PLAIN_TEXT);
    Logger.log("File created.");
  }
}

function saveLogToFolder(data) {
  var folder = DriveApp.getFolderById(folderLog);
  var fileName =
    "dataPost_" + data.id_device + "-" + new Date().toISOString() + ".txt";
  var file = folder.getFilesByName(fileName);

  var jsonString = JSON.stringify(data, null, 2);

  if (file.hasNext()) {
    var existingFile = file.next();
    existingFile.setContent(jsonString); // Ghi đè nội dung
    Logger.log("File already exists. Overwritten.");
  } else {
    folder.createFile(fileName, jsonString, MimeType.PLAIN_TEXT);
    Logger.log("File created.");
  }
}

function test_saveAmplificationToFolder() {
  var testData = {
    method: "append",
    id_device: "RPLTest",
    version: "V2.2.9",
    time: "30-07-2025 11:18:59",
    slopes: [1.367, 1.482, 1.364, 1.316, 1.505, 1.472, 1.3, 1.33, 1.532, 1.548],
    origins: [0, 0, 0, 0, 0, 0, 0, 0, 0, 0],
    LED_power: [150, 150, 150, 150, 150, 150, 150, 150, 150, 150],
    CT_value: [3, 31, 25.3, 30.7, 30.7, 25.3, 30.3, 39.3, 30.3, 25.3],
    result: [
      "-- | N",
      "-- | N",
      "-- | N",
      "-- | N",
      "-- | N",
      "-- | N",
      "-- | N",
      "-- | N",
      "-- | N",
      "-- | N",
    ],
    outcome: [
      {
        transition_time: {
          x: 3,
          y: -3.7,
          i: 9,
        },
        plateau_point: {
          x: 5,
          y: 1.1,
          i: 15,
        },
        increase: 4.8,
      },
      {
        transition_time: {
          x: 31,
          y: -2.8,
          i: 93,
        },
        plateau_point: {
          x: 32.7,
          y: 3.5,
          i: 98,
        },
        increase: 6.3,
      },
      {
        transition_time: {
          x: 25.3,
          y: -4.4,
          i: 76,
        },
        plateau_point: {
          x: 27,
          y: 0.7,
          i: 81,
        },
        increase: 5.1,
      },
      {
        transition_time: {
          x: 30.7,
          y: -3.8,
          i: 92,
        },
        plateau_point: {
          x: 32.7,
          y: 2.1,
          i: 98,
        },
        increase: 5.9,
      },
      {
        transition_time: {
          x: 30.7,
          y: -2.3,
          i: 92,
        },
        plateau_point: {
          x: 32.3,
          y: 1.1,
          i: 97,
        },
        increase: 3.5,
      },
      {
        transition_time: {
          x: 25.3,
          y: -3.3,
          i: 76,
        },
        plateau_point: {
          x: 27,
          y: 1.1,
          i: 81,
        },
        increase: 4.3,
      },
      {
        transition_time: {
          x: 30.3,
          y: -0.4,
          i: 91,
        },
        plateau_point: {
          x: 32.3,
          y: 3.3,
          i: 97,
        },
        increase: 3.7,
      },
      {
        transition_time: {
          x: 39.3,
          y: -0.3,
          i: 118,
        },
        plateau_point: {
          x: 39.7,
          y: 1.2,
          i: 119,
        },
        increase: 1.5,
      },
      {
        transition_time: {
          x: 30.3,
          y: -2.1,
          i: 91,
        },
        plateau_point: {
          x: 32.3,
          y: 2.5,
          i: 97,
        },
        increase: 4.6,
      },
      {
        transition_time: {
          x: 25.3,
          y: -3.2,
          i: 76,
        },
        plateau_point: {
          x: 27,
          y: 0.7,
          i: 81,
        },
        increase: 3.9,
      },
    ],
    peak_features: [
      {
        main_peak: {
          x: 4.3,
          y: 3.3,
          i: 13,
        },
        right_arm: {
          x: 4.7,
          y: 2.6,
          i: 14,
        },
        left_arm: {
          x: 4,
          y: 2.2,
          i: 12,
        },
      },
      {
        main_peak: {
          x: 32.3,
          y: 4,
          i: 97,
        },
        right_arm: {
          x: 32.7,
          y: 0.8,
          i: 98,
        },
        left_arm: {
          x: 31.3,
          y: 3.4,
          i: 94,
        },
      },
      {
        main_peak: {
          x: 26.3,
          y: 4.6,
          i: 79,
        },
        right_arm: {
          x: 26.7,
          y: 3.3,
          i: 80,
        },
        left_arm: {
          x: 26,
          y: 3.3,
          i: 78,
        },
      },
      {
        main_peak: {
          x: 31.3,
          y: 4.1,
          i: 94,
        },
        right_arm: {
          x: 32,
          y: 3.6,
          i: 96,
        },
        left_arm: {
          x: 31,
          y: 2.1,
          i: 93,
        },
      },
      {
        main_peak: {
          x: 31.7,
          y: 3.2,
          i: 95,
        },
        right_arm: {
          x: 32,
          y: 2.2,
          i: 96,
        },
        left_arm: {
          x: 31.3,
          y: 2.4,
          i: 94,
        },
      },
      {
        main_peak: {
          x: 26.3,
          y: 4.3,
          i: 79,
        },
        right_arm: {
          x: 26.7,
          y: 1.8,
          i: 80,
        },
        left_arm: {
          x: 26,
          y: 3.7,
          i: 78,
        },
      },
      {
        main_peak: {
          x: 31.7,
          y: 2.8,
          i: 95,
        },
        right_arm: {
          x: 32,
          y: 1.6,
          i: 96,
        },
        left_arm: {
          x: 31,
          y: 1.8,
          i: 93,
        },
      },
      {
        main_peak: {
          x: 39.7,
          y: 4.5,
          i: 119,
        },
        right_arm: {
          x: -1,
          y: -1,
          i: -1,
        },
        left_arm: {
          x: 39.3,
          y: 1.4,
          i: 118,
        },
      },
      {
        main_peak: {
          x: 31,
          y: 3.1,
          i: 93,
        },
        right_arm: {
          x: 31.3,
          y: 2.3,
          i: 94,
        },
        left_arm: {
          x: 30.7,
          y: 2,
          i: 92,
        },
      },
      {
        main_peak: {
          x: 26,
          y: 3.3,
          i: 78,
        },
        right_arm: {
          x: 26.7,
          y: 1.7,
          i: 80,
        },
        left_arm: {
          x: 25.7,
          y: 2.1,
          i: 77,
        },
      },
    ],
    amplification: [
      "212,212,214,215,217,218,311,312,307,312,312,309,315,315,316,318,318,318,316,315,318,319,318,318,315,313,313,313,320,314,312,314,312,314,314,318,319,319,316,319,313,312,312,312,312,308,314,311,309,306,308,314,310,313,312,313,310,312,310,312,311,314,313,312,313,314,312,312,312,312,308,309,313,313,307,305,310,309,306,307,311,312,309,303,304,308,312,311,305,305,310,311,307,307,307,308,312,311,310,311,313,305,305,310,304,307,306,309,306,306,311,314,312,308,308,306,314,313,309,310,",
      "415,420,420,419,421,422,559,555,554,556,555,558,557,562,564,564,564,560,560,560,561,561,564,563,560,557,560,555,560,563,557,562,557,563,563,566,566,568,563,564,559,554,555,557,553,556,558,560,559,559,555,559,557,558,559,560,555,559,559,560,562,560,560,562,561,565,560,563,564,566,560,557,568,558,559,557,558,557,558,556,563,568,566,559,559,560,560,561,556,558,558,557,556,555,556,556,566,564,565,565,567,559,559,560,559,558,556,559,560,562,566,567,563,565,567,564,564,560,560,560,",
      "307,307,310,310,312,312,439,439,439,440,440,441,440,443,448,443,448,444,447,445,442,444,448,448,448,442,442,442,447,447,445,447,444,448,446,448,448,446,444,444,441,440,437,440,440,438,443,440,440,441,442,441,440,440,444,444,442,444,444,441,444,444,440,442,439,443,444,447,448,446,439,438,446,437,435,439,439,437,437,440,444,449,448,441,439,440,444,443,441,441,442,440,443,441,440,444,448,446,448,443,445,440,439,440,439,439,438,437,441,438,446,445,441,445,445,448,447,447,443,444,",
      "303,304,306,305,305,304,416,416,416,416,415,417,419,420,423,422,423,421,422,423,421,424,423,423,424,420,417,419,423,423,418,421,420,424,423,424,424,426,421,421,415,412,419,417,417,416,417,418,416,415,416,416,416,416,416,416,416,420,418,416,416,419,418,420,423,421,424,421,423,419,416,416,422,415,417,419,418,417,417,424,421,423,424,417,416,412,420,418,414,417,418,413,415,415,416,418,422,424,424,423,423,416,416,417,416,417,417,416,417,418,422,420,420,423,422,420,424,419,419,420,",
      "293,291,294,295,297,294,408,407,407,402,407,407,408,407,409,409,408,412,410,410,411,413,410,413,411,408,405,410,409,409,408,407,408,411,415,413,415,409,413,411,408,406,405,404,402,404,408,404,405,404,406,407,406,405,405,405,404,408,408,407,408,408,407,407,408,408,411,410,412,413,407,408,413,408,405,407,408,407,406,408,412,411,413,405,404,408,409,406,404,405,405,408,405,403,405,408,412,413,411,408,411,408,406,408,407,406,408,407,407,408,411,412,410,411,408,410,408,407,408,408,",
      "325,324,328,328,328,328,442,441,446,447,448,448,445,448,452,450,451,448,448,448,450,449,454,452,449,446,448,448,449,450,448,446,450,454,455,450,451,452,448,449,442,444,442,443,442,441,446,443,448,447,448,445,446,448,447,449,448,445,447,448,447,447,446,449,448,451,451,451,451,448,445,441,452,442,443,444,443,444,445,448,450,455,453,445,442,443,452,447,443,441,448,448,446,443,448,453,453,452,456,451,451,447,448,450,448,448,445,446,449,447,453,451,447,449,450,450,452,449,448,449,",
      "428,435,432,430,430,428,510,511,514,512,511,511,513,512,513,514,516,513,512,512,513,514,512,514,514,512,512,512,512,512,512,513,516,515,512,513,513,512,512,512,509,509,508,511,510,510,510,512,510,511,512,512,512,512,509,512,513,515,514,514,514,512,514,514,516,517,515,517,514,512,512,511,515,508,510,511,511,510,509,512,512,513,515,511,512,512,519,511,512,512,514,511,512,512,514,518,515,519,517,516,520,511,512,512,515,514,512,513,513,515,516,515,515,515,513,516,514,512,512,512,",
      "280,280,280,280,282,286,378,379,380,382,377,382,383,383,383,384,385,384,383,380,384,381,383,384,380,380,380,376,384,386,384,380,384,385,385,382,384,384,385,385,377,377,377,376,377,378,376,379,378,378,378,378,377,378,380,378,378,375,376,376,376,382,376,378,382,383,384,385,385,384,376,376,384,378,378,376,378,380,380,383,380,384,380,377,378,380,380,378,377,377,376,378,377,377,378,383,383,380,379,378,379,376,376,381,382,376,376,377,383,378,383,382,380,384,384,384,383,383,380,384,",
      "352,358,352,352,352,351,453,454,451,454,451,456,457,456,457,458,459,461,458,456,463,463,463,460,459,456,456,458,457,460,460,456,462,460,458,459,461,456,457,457,453,453,453,454,451,454,454,455,452,455,456,455,456,456,457,456,458,457,459,456,456,459,455,459,458,459,460,460,461,458,456,456,458,456,454,457,456,456,456,459,458,460,462,455,453,452,460,454,455,454,455,452,455,456,456,463,460,459,463,462,459,455,455,457,459,455,454,456,456,457,461,460,457,458,457,459,457,458,455,455,",
      "329,336,335,330,332,336,454,455,456,456,457,458,463,456,461,462,463,461,464,459,463,464,464,464,462,462,461,462,460,462,463,461,468,458,461,461,462,462,462,460,458,456,458,456,456,456,457,456,456,457,458,457,463,458,457,460,460,463,460,459,461,462,463,464,464,464,462,463,464,460,455,456,457,459,454,456,456,456,456,458,463,466,463,458,457,456,462,457,456,456,457,456,457,459,458,463,465,463,464,463,461,456,459,460,459,458,458,458,458,461,462,462,462,463,464,461,464,461,457,459,",
    ],
    kitId: 10.0,
  };
  sheetResult(testData); // Ghi dữ liệu vào sheet Result
  sheetData(testData); // Ghi dữ liệu vào sheet Data
  saveAmplificationToFolder(testData);
  highlightAmplificationRows();
}

function test_saveError() {
  var testData = {
    method: "error",
    id_device: "RPL02007",
    version: "v2.3.9",
    error: [
      {
        Slot: "Slot 6",
        error_code: "1045",
        error_msg:
          "[Sensor Light]- No data from sensor In Process Amplification 40 min ",
      },
      {
        Slot: "Slot 7",
        error_code: "1046",
        error_msg:
          "[Sensor Light]- No data from sensor In Process Amplification 40 min ",
      },
      {
        Slot: "Slot 8",
        error_code: "1047",
        error_msg: "[Sensor Light]- Sensor too dark In Process Lysis 10 min ",
      },
    ],
  };
  sheetError(testData);
  // saveErrorToFolder(testData);
  // sheetResult(testData); // Ghi dữ liệu vào sheet Result
  // sheetData(testData); // Ghi dữ liệu vào sheet Data
  // saveAmplificationToFolder(testData);
  // highlightAmplificationRows();
}

/* Tô màu MỘT hàng Amplification vừa thêm — gọi từ sheetData (nhẹ, O(1)). */
function highlightAmplificationRow_(sheet, row) {
  var lastCol = Math.max(sheet.getLastColumn(), 1);
  sheet.getRange(row, 1, 1, lastCol).setBackground("#cfe9ff");
}

/* (Tiện ích chạy TAY 1 lần) Tô lại toàn bộ hàng Amplification cũ.
 * KHÔNG gọi trong doPost vì quét cả sheet rất chậm khi dữ liệu lớn. */
function highlightAmplificationRows() {
  var sheet = SpreadsheetApp.openById(sheet_id).getSheetByName("Data");
  var range = sheet.getDataRange(); // lấy toàn bộ dữ liệu
  var values = range.getValues(); // mảng giá trị
  var numCols = range.getNumColumns();

  for (var i = 0; i < values.length; i++) {
    if (values[i][0] === "Amplification") {
      // kiểm tra cột A
      sheet.getRange(i + 1, 1, 1, numCols).setBackground("#cfe9ff"); // tô hàng đó màu vàng (mã hex)
    }
  }
}
function onOpen(e) {
  const ss = SpreadsheetApp.getActiveSpreadsheet();
  const sheets = ss.getSheets();

  sheets.forEach((sheet) => {
    const lastRow = sheet.getLastRow();
    const lastCol = sheet.getLastColumn();

    if (lastRow > 0 && lastCol > 0) {
      sheet.setActiveRange(sheet.getRange(lastRow, 1, 1, lastCol));
    }
  });
}

/* =====================================================================
 *  doGet — API ĐỌC LỊCH SỬ TỪ DRIVE (cho app FBT_RAPID)
 *  ---------------------------------------------------------------------
 *  Cùng project Apps Script với doPost ở trên (dùng chung `folderId`).
 *  ⚠️ Sau khi thêm hàm này PHẢI DEPLOY LẠI web app
 *     (Deploy → Manage deployments → Edit → New version) thì URL /exec
 *     mới nhận doGet.
 *
 *  Tham số trên query string (URL `...?...`):
 *    ?action=ids                         → danh sách ID máy + số lần chạy + lần mới nhất
 *    ?action=runs&id=<ID>                → tóm tắt các lần chạy của 1 máy (KHÔNG kèm đường cong)
 *           [&limit=<n>][&offset=<n>]       phân trang (mặc định limit=50, mới nhất trước)
 *    ?action=run&fileId=<FILE_ID>        → chi tiết 1 lần chạy (kèm curves + outcome/peak)
 *    ?action=peek                        → xem 1 file mẫu để kiểm tra định dạng (debug)
 *    [&callback=<fn>]                    → (tuỳ chọn) bọc JSONP cho client web
 *
 *  Mọi response là JSON: thành công có "ok":true; lỗi có "ok":false,"error".
 *  Lưu ý CORS: app Flutter desktop/mobile KHÔNG bị CORS (chỉ Flutter Web mới bị).
 * ===================================================================== */

const LOG_SCAN_LIMIT = 5000; // chặn quét vô hạn nếu folder quá lớn
const RUNS_DEFAULT_LIMIT = 50; // mỗi run là 1 file lớn (có amplification) phải tải nguyên → giữ thấp

function doGet(e) {
  const p = (e && e.parameter) || {};
  const action = (p.action || "ids").toLowerCase();
  let body;
  try {
    if (action === "ids")
      body = apiListDeviceIds_(p.nocache === "1" || p.fresh === "1");
    else if (action === "runs") body = apiListRuns_(p.id, p.limit, p.offset);
    else if (action === "run") body = apiGetRun_(p.fileId);
    else if (action === "peek") body = apiPeek_();
    else body = { ok: false, error: "action không hợp lệ: " + action };
  } catch (err) {
    body = { ok: false, error: String((err && err.message) || err) };
  }
  return reply_(body, p.callback);
}

function reply_(obj, callback) {
  const json = JSON.stringify(obj);
  if (callback) {
    return ContentService.createTextOutput(
      callback + "(" + json + ")",
    ).setMimeType(ContentService.MimeType.JAVASCRIPT);
  }
  return ContentService.createTextOutput(json).setMimeType(
    ContentService.MimeType.JSON,
  );
}

/* ---- Thu thập file log: folder gốc + mọi thư mục con (đề phòng đã gom theo ID) ---- */
function collectLogFiles_() {
  const root = DriveApp.getFolderById(folderId);
  const out = [];
  const stack = [root];
  while (stack.length && out.length < LOG_SCAN_LIMIT) {
    const f = stack.pop();
    const it = f.getFiles();
    while (it.hasNext() && out.length < LOG_SCAN_LIMIT) {
      const file = it.next();
      if (/^Log_.*\.txt$/i.test(file.getName())) out.push(file);
    }
    const sub = f.getFolders();
    while (sub.hasNext()) stack.push(sub.next());
  }
  return out;
}

/* ---- Tách ID máy từ tên file: Log_<ID>-<timestamp>.txt ---- */
function extractIdFromName_(name) {
  let core = name.replace(/^Log_/i, "").replace(/\.txt$/i, "");
  core = core
    .replace(/-\d{4}-\d{2}-\d{2}T[\d:.]+Z?$/, "") // ISO 2025-07-30T11:18:59.123Z
    .replace(/-\d{2}-\d{2}-\d{4}[ _]\d{2}[:._-]\d{2}[:._-]\d{2}$/, "") // dd-mm-yyyy HH:mm:ss
    .replace(/-N\/A$/i, ""); // file cũ đặt tên Log_<id>-N/A.txt (firmware gửi time="N/A")
  return core.trim();
}

/* ---- Lấy mốc thời gian (ISO) ngay từ tên file → khỏi gọi getDateCreated() ---- */
function extractTimeFromName_(name) {
  let m = name.match(/-(\d{4}-\d{2}-\d{2}T[\d:.]+Z?)\.txt$/i); // ISO sẵn trong tên
  if (m) return m[1];
  m = name.match(
    /-(\d{2})-(\d{2})-(\d{4})[ _](\d{2})[:._-](\d{2})[:._-](\d{2})\.txt$/i,
  ); // dd-mm-yyyy HH:mm:ss
  if (m)
    return (
      m[3] + "-" + m[2] + "-" + m[1] + "T" + m[4] + ":" + m[5] + ":" + m[6]
    );
  return "";
}

/* ---- Tìm nhanh file của 1 máy bằng Drive search (không duyệt cả folder) ---- */
function searchLogFilesById_(id) {
  const folder = DriveApp.getFolderById(folderId);
  const safe = String(id).replace(/'/g, "\\'");
  const it = folder.searchFiles("title contains 'Log_" + safe + "-'");
  const out = [];
  while (it.hasNext() && out.length < LOG_SCAN_LIMIT) {
    const file = it.next();
    const name = file.getName();
    // 'contains' có thể khớp ID là tiền tố của ID khác → lọc lại cho chắc
    if (
      /^Log_.*\.txt$/i.test(name) &&
      extractIdFromName_(name).toLowerCase() === String(id).toLowerCase()
    ) {
      out.push(file);
    }
  }
  return out;
}

/* ---- File có nằm trực tiếp trong folder cấu hình không? (chặn đọc file lạ) ---- */
function fileInFolder_(file, fid) {
  const it = file.getParents();
  while (it.hasNext()) {
    if (it.next().getId() === fid) return true;
  }
  return false;
}

function apiListDeviceIds_(forceFresh) {
  // Server cache 5 phút: ids quét cả folder + đọc version nên rất chậm (~25–50s).
  // Lần gọi đầu tính xong được lưu trên máy chủ Google; các lần sau (MỌI
  // máy/người dùng) trong 5 phút trả ngay từ cache. forceFresh=true (nút "Làm
  // mới") thì bỏ qua cache để lấy dữ liệu mới nhất.
  const cache = CacheService.getScriptCache();
  if (!forceFresh) {
    const hit = cache.get("ids_v1");
    if (hit) {
      const obj = JSON.parse(hit);
      obj.cached = true; // đánh dấu đã lấy từ cache (để debug)
      return obj;
    }
  }

  const files = collectLogFiles_();
  const map = {}; // id -> {id, runCount, latest, latestFile}
  files.forEach(function (file) {
    const name = file.getName();
    const id = extractIdFromName_(name) || "(unknown)";
    const t = extractTimeFromName_(name); // ISO từ tên file, "" nếu không khớp
    const cur = map[id] || {
      id: id,
      runCount: 0,
      latest: "",
      latestFile: null,
    };
    cur.runCount++;
    // Giữ file MỚI NHẤT của máy để đọc version firmware mới nhất.
    if (!cur.latestFile || (t && t > cur.latest)) {
      if (t) cur.latest = t;
      cur.latestFile = file;
    }
    map[id] = cur;
  });
  const devices = Object.keys(map)
    .map(function (k) {
      const d = map[k];
      let version = "";
      try {
        // chỉ đọc 1 file/máy (file mới nhất) để lấy version → không quét hết.
        version = extractVersion_(d.latestFile.getBlob().getDataAsString());
      } catch (e) {}
      return {
        id: d.id,
        runCount: d.runCount,
        latest: d.latest,
        version: version,
      };
    })
    .sort(function (a, b) {
      return String(b.latest).localeCompare(String(a.latest));
    });
  const result = {
    ok: true,
    folderId: folderId,
    count: devices.length,
    devices: devices,
    cached: false,
  };
  try {
    cache.put("ids_v1", JSON.stringify(result), 300); // lưu 5 phút (bỏ qua nếu >100KB)
  } catch (e) {}
  return result;
}

function apiListRuns_(id, limit, offset) {
  if (!id) return { ok: false, error: "thiếu tham số id" };
  const lim = Math.max(
    1,
    Math.min(parseInt(limit, 10) || RUNS_DEFAULT_LIMIT, 1000),
  );
  const off = Math.max(0, parseInt(offset, 10) || 0);
  const files = searchLogFilesById_(id); // chỉ file của đúng máy này
  files.sort(function (a, b) {
    // mới nhất trước, theo mốc thời gian trong tên file (ISO so sánh được trực tiếp)
    return String(extractTimeFromName_(b.getName())).localeCompare(
      String(extractTimeFromName_(a.getName())),
    );
  });
  const total = files.length;
  const page = files.slice(off, off + lim);
  const runs = page.map(function (file) {
    return summarizeRun_(file, parseLog_(file.getBlob().getDataAsString())); // KHÔNG kèm curves
  });
  return {
    ok: true,
    id: id,
    total: total,
    offset: off,
    limit: lim,
    count: runs.length,
    runs: runs,
  };
}

function apiGetRun_(fileId) {
  if (!fileId) return { ok: false, error: "thiếu tham số fileId" };
  const file = DriveApp.getFileById(fileId);
  // Chốt chặn bảo mật: web app chạy với quyền chủ sở hữu + "Anyone" → chỉ cho
  // đọc file log Log_*.txt nằm ĐÚNG trong folder cấu hình, không đọc file lạ.
  if (
    !/^Log_.*\.txt$/i.test(file.getName()) ||
    !fileInFolder_(file, folderId)
  ) {
    return {
      ok: false,
      error: "fileId không phải file log hợp lệ trong folder",
    };
  }
  const obj = parseLog_(file.getBlob().getDataAsString());
  const run = summarizeRun_(file, obj);
  run.curves = parseCurves_(obj.amplification);
  run.loops = run.curves.length ? run.curves[0].length : 0;
  run.outcomeDetail = obj.outcome || null;
  run.peakFeatures = obj.peak_features || null;
  run.slopes = obj.slopes || []; // để app calibrate (raw/slope) khi vẽ đồ thị
  run.origins = obj.origins || [];
  return { ok: true, run: run };
}

function apiPeek_() {
  const files = collectLogFiles_();
  if (!files.length)
    return {
      ok: true,
      count: 0,
      note: "folder rỗng / không có file Log_*.txt",
    };
  const file = files[0];
  const content = file.getBlob().getDataAsString();
  let format = "text";
  try {
    JSON.parse(content);
    format = "json";
  } catch (e) {}
  return {
    ok: true,
    count: files.length,
    sample: {
      fileId: file.getId(),
      fileName: file.getName(),
      created: file.getDateCreated().toISOString(),
      detectedFormat: format,
      idFromName: extractIdFromName_(file.getName()),
      head: content.substring(0, 600),
    },
  };
}

/* ---- Parse 1 file log: JSON (getData.js) hoặc text (AppScript.js) ---- */
function parseLog_(content) {
  try {
    return JSON.parse(content);
  } catch (e) {
    return parseTextLog_(content);
  }
}

function parseTextLog_(content) {
  const obj = {
    id_device: "",
    version: "",
    time: "",
    CT_value: [],
    result: [],
    amplification: [],
  };
  String(content)
    .split(/\r?\n/)
    .forEach(function (line) {
      let m;
      if ((m = line.match(/^Device ID:\s*(.*)$/i))) obj.id_device = m[1].trim();
      else if ((m = line.match(/^Version:\s*(.*)$/i)))
        obj.version = m[1].trim();
      else if ((m = line.match(/^Time:\s*(.*)$/i))) obj.time = m[1].trim();
      else if ((m = line.match(/^CT Values?:\s*(.*)$/i)))
        obj.CT_value = m[1].split(",").map(function (s) {
          return s.trim();
        });
      else if ((m = line.match(/^Result:\s*(.*)$/i)))
        obj.result = m[1]
          .split("|")
          .map(function (s) {
            return s.trim();
          })
          .filter(String);
      else if ((m = line.match(/^\s*Row\s*\d+:\s*(.*)$/i)))
        obj.amplification.push(m[1].trim());
    });
  return obj;
}

/* ---- Lấy nhanh version firmware từ nội dung file (khỏi parse toàn bộ JSON) ---- */
function extractVersion_(content) {
  if (!content) return "";
  let m = content.match(/"version"\s*:\s*"?([^"\n,}]+)"?/i); // file JSON
  if (m) return m[1].trim();
  m = content.match(/^\s*Version:\s*(.*)$/im); // file text cũ
  if (m) return m[1].trim();
  return "";
}

/* ---- Chuẩn hoá kết quả 1 slot -> P | N | S | E | ? ---- */
function classifyLetter_(seg) {
  if (seg == null) return "?";
  const t = String(seg).toUpperCase();
  if (t.indexOf("SLIDE") >= 0 || t.indexOf("SLIGHT") >= 0) return "S";
  if (t.indexOf("/E") >= 0) return "E";
  if (t.indexOf("POSITIVE") >= 0) return "P";
  if (t.indexOf("NEGATIVE") >= 0) return "N";
  if (t.indexOf("ERROR") >= 0) return "E";
  const tail = t.indexOf("|") >= 0 ? t.split("|").pop() : t;
  const c = tail.replace(/[^A-Z]/g, "").charAt(0);
  return c === "P" || c === "N" || c === "S" || c === "E" ? c : "?";
}

const RESULT_LABEL_ = {
  P: "Positive",
  N: "Negative",
  S: "Slide Positive",
  E: "Error",
  "?": "Unknown",
};

function summarizeRun_(file, obj) {
  const result = (obj.result || []).map(classifyLetter_);
  const counts = {
    positive: 0,
    negative: 0,
    slightPositive: 0,
    error: 0,
    unknown: 0,
  };
  result.forEach(function (r) {
    if (r === "P") counts.positive++;
    else if (r === "N") counts.negative++;
    else if (r === "S") counts.slightPositive++;
    else if (r === "E") counts.error++;
    else counts.unknown++;
  });
  const name = file.getName();
  const nameTime = extractTimeFromName_(name); // ISO từ tên file (≈ ngày tạo, do server stamp lúc lưu)
  const created = nameTime || file.getDateCreated().toISOString();
  const time =
    obj.time && String(obj.time).trim() ? String(obj.time).trim() : created;
  return {
    fileId: file.getId(),
    fileName: name,
    id_device: obj.id_device || extractIdFromName_(name),
    version: obj.version || "",
    time: time,
    created: created,
    ct: obj.CT_value || [],
    result: result,
    resultLabels: result.map(function (r) {
      return RESULT_LABEL_[r];
    }),
    counts: counts,
  };
}

function parseCurves_(amp) {
  if (!amp || !amp.length) return [];
  return amp.map(function (row) {
    if (Object.prototype.toString.call(row) === "[object Array]")
      return row.map(Number);
    return String(row)
      .split(",")
      .filter(function (x) {
        return x.trim() !== "";
      })
      .map(Number);
  });
}

/* ---- Test nhanh ngay trong editor Apps Script (Run → chọn hàm) ---- */
function test_doGet_ids() {
  Logger.log(doGet({ parameter: { action: "ids" } }).getContent());
}
function test_doGet_peek() {
  Logger.log(doGet({ parameter: { action: "peek" } }).getContent());
}
