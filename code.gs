/* Fills in the Sheet with timestamp of high water level event in Col A, or water
   level back to normal event in Col B, and calculates time difference in Col C. 
   Adds headers if not there.
   This script works based on single parameter being transferred, namely "statu=1" or "status=0"
   it will read multiple parameters, but only updates based on just one.
   From perspective of ESP32 code, doGet seems counter intuitive to doPost.
   It processes the context stream, in this case only the "status" key, and sends
   response back to the ESP32, hence the doGet().
*/
var timeZone = "CST"; //see https://www.timeanddate.com/time/zones/
var dateTimeFormat = "MM/dd/yyyy HH:mm:ss";
var column;
var resultType = "text"; //text"; // for testing in web page, set to "html" or set to "text" for ESP return
var enableSendingEmails = true;
var emailAddress = "somebodys_email@gmail.com"; // comma separate for several emails

//To prevent Google from shutting down account because of excessive emails, allow emails 
//only every 4 hours,not every alert recorded.
var emailTime = 240.0; //60 minutes x 4 = 240 minutes 
//Google sheet ID 
GS_ID = "lkjhgfdsaPOIUYTREWQMNBVCXZ98765432IUYTREW";  //GET FROM Google Sheet
/*
          HOW TO GET FROM SPREADSHEET NAME IN GOOGLE DRIVE:
          var FileIterator = DriveApp.getFilesByName(FileNameString);
          while (FileIterator.hasNext()) {
            var file = FileIterator.next();
            if (file.getName() == FileNameString) {
              var ss = SpreadsheetApp.open(file);
              var fileID = file.getId();
            }    
         }
*/

//open the spreadsheet, using the Id obtained from sheet url
var ss = SpreadsheetApp.openById(GS_ID);
Logger.log('sheet name is: ' + ss.getName());
//get sheet to add data to
var sheet = ss.getSheetByName("Sheet1"); 

function fakeGet() {
  //used to Debug Google Sheets; choose this function to start Debug.
  //uses same form of underlying doGet process. 
  //Change "status" to "1" or "0" in 3 places to run for high or normal respectively.
  var eventObject = 
    {
      "parameter": {
        "status": "1"
      },
      "contextPath": "",
      "contentLength": -1,
      "queryString": "status=1",
      "parameters": {
        "status": ["1"],
      }
    }
  doGet(eventObject);
}

function doGet(e) {
  //e is the url as JSON object
  var result = 'doGet Ok'; // default result
  Logger.log("Starting");
  Logger.log(JSON.stringify(e));
  //Logger.log('query parameters: ' + e.parameters); //only indicates object
  if ((e.parameter !== '') && (e.parameter !== "undefined")) {
    var status = stripQuotes(e.parameter.status);
    Logger.log('status found is: ' + status);
    if (typeof status != 'undefined') {
          var rowData = [];      
      //automatically create headers if first row; redundant after first time.
      //sheet is globally defined.
      if (sheet.getRange("A1").getValue().isBlank) {
         var HeaderRow=["Water Level High", "Water Level Normal", "Resolved Time. Min."];
        for (var i = 0; i<HeaderRow.length; i++) {
          rowData[i] = HeaderRow[i];
        }
        var rng = sheet.getRange(1,1,1,HeaderRow.length)
        rng.setValues([rowData]); 
        var rowFont = ["bold", "bold", "bold"];
        rng.setFontWeights([rowFont]);
        rng.setWrapStrategy(SpreadsheetApp.WrapStrategy.WRAP);
      } 
 
      //queryString is std fcn, everything after "exec?"" in url.
      //gets the parameter names, i.e., key values, e.g., status
      var namesOfParams = [];
      for (var param in parseQuery(e.queryString)) {
         namesOfParams.push(param);
         }
          
      //get last used cell in col A
      var lastRowA = lastRowIndexInColumn("A"); 
      Logger.log('Lastcell: ' + lastRowA);
      newRow = lastRowA + 1;
      var d1;
      var d2;
      var dtime;
      var timediff;     
      if (newRow >= 1) {
        if (e.parameter == 'undefined') {
          sheet.getRange(newRow, 2).setValue("undefined");
          result += " Missing status key"
         } else {
            for (var i=0; i<namesOfParams.length;i++) {
              var value = stripQuotes(e.parameter[namesOfParams[i]]);        
             //namesOfParams=namesOfParams.reverse();
             //place the timestamp and status code, as array and set array to new row
              dtime = Utilities.formatDate(new Date(), timeZone, dateTimeFormat);
             
             if (value == 1) { 
               sheet.getRange(newRow,1).setValue(dtime);
               result += "Alert timestamp Added;"
               //is last email time over the minumum time allowed?
               //get the time diff. and convert to minutes
               d1 = new Date(sheet.getRange(lastRowA,1).getValue()).getTime();
               d2 = new Date(sheet.getRange(newRow,1).getValue()).getTime();
               timediff = ((d2-d1)/(1000.0*60.0)); //in minutes
               Logger.log('time span is ' + timediff);
               if (timediff >= emailTime) {
                  //call local fcn to send email with MailApp.sendMail fcn
                  sendmail("Alert. Sump Pump High Water Level Detected!", "Sump Pump High Level Warning. Check sump pump NOW!!!!!" );
                 }
                result += " email sent"; 
              }
              //the way the trigger is set in ESP sketch, status=0 occurs only when the system 
              //goes back to sleep after indicating return to normal.
              //place value in col B at last high level result row, but only if last row, col B cell is blank
              //this will prevent overwriting col B entry on very first power up or after power failure.
              if (value == 0) {
                if (sheet.getRange(lastRowA, 2).isBlank()){
                  sheet.getRange(lastRowA, 2).setValue(dtime); //add low level datetime to col B
                  result += " Normal level timestamp added;"
                  sendmail( "Sump Pump Level Normal Now", "Sump Pump water level has returned to normal.");
                 }
                 result += " email sent;";
              }
             }
             //Get the difference in minutes between last two alerts. raw values are millisec
             //The system cannot register a time less than 1 minutes, because we 
             d1 = new Date(sheet.getRange(lastRowA,2).getValue()).getTime();
             d2 = new Date(sheet.getRange(lastRowA,1).getValue()).getTime();
             timediff = (1.0*(d1-d2)/(1000*60)); //in minutes
             Logger.log('timediff is ' + timediff);
             if (value == 0) {
               sheet.getRange(lastRowA,3).setValue(timediff);
              }
           } 
       } else{     
           result += " lastrow < 1";
         }  
     } else {
       result += " status undefined";
      }
    
   } else {
       result += " undefined parameter";
       }          
  // Because we invoked the script as a web app, it requires we return a ContentService object.
  return returnContent(result);
}  

// Remove leading and trailing single or double quotes
function stripQuotes(value) {
   return value.replace(/^["']|['"]$/g, "");
 }
 
function parseQuery(queryString) {
   var query = {};
   //fyi: querysting is everything after exec?
   var pairs = (queryString[0] === '?' ? queryString.substr(1) : queryString).split('&');
   //Logger.log('query pairs are: ' + pairs);
      for (var i = 0; i < pairs.length; i++) {
       var pair = pairs[i].split('=');
       query[decodeURIComponent(pair[0])] = decodeURIComponent(pair[1] || '');
     }
   return query;
 } 

function sendmail(subject, message) {
   if (!enableSendingEmails) {
     return;
   }
   Logger.log('invoking MailApp');
   MailApp.sendEmail(emailAddress, subject, message);
 }

function returnContent(result) {
  if (resultType == "html") {
    //To test, copy the Google sheet script url when authenticating and
    //paste url in browser. After "exec" if deployed as New Deployment Web App, or 
    // after "dev", if using "Test Deployment", add ?status=1, or status=0 (no quotes)  
    var MyHtml = "<!doctype html><html><body><h1>Content returned from script</h1><p>" + result + "</p></body></html>";
    // another example 
    //var MyHtlm = "<!doctype html><html><body><h1>Content returned from script</h1><p>" + result + "</p><p>" + e.parameter.status + "</p></body></html>");
    HtmlService.createHtmlOutput(MyHtml); 
   }    
  if (resultType == "text") {
    //used for sending data back to ESP32
    Logger.log('result is ' + result)
    ContentService.createTextOutput(result);
   }
 }   

// best answer yet for next blank row in column: https://stackoverflow.com/questions/26056370/find-lastrow-of-column-c-when-col-a-and-b-have-a-different-row-size

function lastRowIndexInColumn(column) {
   //column must be column letter, e.g. "A"
   //returns last used cell, not next cell
   //fails if there are spaces. If spaces used .getMaxRows()
   var lastRow = sheet.getLastRow();
   if (lastRow > 1) {
     var values = sheet.getRange(column + "1:" + column + lastRow).getValues();
     // format for loop allowed, with double test condition; allowed  and" (;" if var previously defined
     for (; values[lastRow-1] == "" && lastRow > 0; lastRow--) { }
   }
   //to return last value rather than index: return values[lastRow-1)
   return lastRow;
 }

function getActiveSheetId(){
  //Not used directly...utility for editing code.
  var id  = SpreadsheetApp.getActiveSheet().getSheetId();
  Logger.log(id.toString());
  return id;
} 
