function doPost(e) {
    try {
      const payload = JSON.parse(e.postData.contents);
      const deviceId = payload.device_id;
      const rows = payload.sheet2;
  
      if (!deviceId || !rows) {
        throw new Error("Thiếu device_id hoặc sheet2");
      }
  
      const folder = DriveApp.getFolderById("YOUR_FOLDER_ID_HERE"); // Thư mục chứa các file thiết bị
  
      // Tìm file theo device_id
      let file;
      const files = folder.getFilesByName(deviceId + ".xlsx"); // hoặc .gsheet
  
      if (files.hasNext()) {
        file = files.next();
      } else {
        // Tạo file mới nếu chưa có
        const newSheet = SpreadsheetApp.create(deviceId);
        file = DriveApp.getFileById(newSheet.getId());
        folder.addFile(file);         // thêm vào thư mục
        DriveApp.getRootFolder().removeFile(file); // bỏ khỏi thư mục gốc (tuỳ chọn)
      }
  
      // Mở file đó để ghi
      const sheetFile = SpreadsheetApp.openById(file.getId());
      const sheet = sheetFile.getSheetByName("Sheet1") || sheetFile.insertSheet("Sheet1");
  
      sheet.clearContents();
      sheet.getRange(1, 1, rows.length, rows[0].length).setValues(rows);
  
      return ContentService.createTextOutput("OK").setMimeType(ContentService.MimeType.TEXT);
  
    } catch (err) {
      return ContentService.createTextOutput("Lỗi: " + err.message).setMimeType(ContentService.MimeType.TEXT);
    }
  }
  