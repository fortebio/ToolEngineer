var YOUR_FOLDER_ID_HERE =
  "https://drive.google.com/drive/folders/15uOILaJrNnVYg751HVgzqgloxt-K1Bzq?hl=vi";

function doPost(e) {
  var jsonData = JSON.parse(e.postData.contents);
  var deviceId = jsonData.id_device || "unknown_device";
  // var timeStr = (jsonData.time || new Date().toISOString()).replace(
  //   /[^a-zA-Z0-9_]/g,
  //   "_"
  // );
  var file = fileSheet(deviceId); // Tìm file theo device_id
  // var spreadsheet;
}

// function checkSheetFile(fileName) {
//   var files = DriveApp.getFilesByName(fileName);
//   if (files.hasNext()) {
//     return true;
//   } else {
//     return false;
//   }
// }

// function createSheetFile(fileName, sheetName) {
//   var checks = checkSheetFile(fileName);
//   if (checks == true) {
//     var files = DriveApp.getFilesByName(fileName);
//     var spreadsheet = SpreadsheetApp.open(files.next());
//   } else {
//     var spreadsheet = SpreadsheetApp.create(fileName);
//   }
// }

function fileSheet(id_device) {
  const folder = DriveApp.getFolderById(YOUR_FOLDER_ID_HERE); // Thư mục chứa các file thiết bị
  var fileName = "Device_" + id_device; // Tên file Google Sheet sẽ là "Device_<id_device>"
  let files = folder.getFilesByName(fileName + ".xlsx"); // hoặc .gsheet

  if (files.hasNext()) {
    file = files.next();
  } else {
    // Tạo file mới nếu chưa có
    const newSheet = SpreadsheetApp.create(fileName);
    file = DriveApp.getFileById(newSheet.getId());
    folder.addFile(file); // thêm vào thư mục
    DriveApp.getRootFolder().removeFile(file); // bỏ khỏi thư mục gốc (tuỳ chọn)
  }
  return file;
}
