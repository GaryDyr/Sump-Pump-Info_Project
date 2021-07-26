/*
  Webhook to get data  from Google Sheet using Google Action Builder console interface, to
  create Conversational actions via Assistaat or Home hub/Mini etc.
  There is no authentication as such from Google Sheets, because triggered the "allow unauthorized invocations".
  Gets sump pump level alert data from Google Sheets with 3 columns. [A] high level date/time,
  [B] low level return, and [C] the difference in minutes betwee the two.
*/

const SHEET_ID = 'llkjhgfdsamnbvcxziuytrewqmnbvcxz987654uytrewjhgf'; //this is the Google Sheet ID


const {
  conversation,
  Simple,
  Card,
  Image,
	} = require('@assistant/conversation');
const functions = require('firebase-functions');
const {google} = require('googleapis');
//Google has moment.js library for inline; see package.json
const moment = require('moment');
moment.locale('US');
const app = conversation({debug:true});
const SHEETS_SCOPE = 'https://www.googleapis.com/auth/spreadsheets.readonly';
var waybackdate = '01/11/2000 12:00';
waybackdate = moment(waybackdate,"MM/DD/YYYY HH:mm").format('MM/DD/YYYY HH:mm'); 

var getSpokenDate = function(date) {
    let adatetime = moment(date, "MM/DD/YYYY HH:mm");
    let dateout = adatetime.format('MMMM DD HH mm');
    console.log('date out is: ' + dateout);
   return dateout;
};

//returns an array of all values from Google Sheets
async function getSheetData() {
  console.log('Getting sheets client.');
  const auth = await google.auth.getClient({
       scopes: [SHEETS_SCOPE],
  });
  client = google.sheets({version: 'v4', auth});
  console.log('Attempting to get data ranges');
  const allTimings = await client.spreadsheets.values.get({
    spreadsheetId: SHEET_ID,
    range: '!A1:C',
    });
  const allEvents = client.spreadsheets.values.get({
    spreadsheetId: SHEET_ID,
    range: '!A1:A',
    });
  timedata = allTimings.data.values;
    //remove header row from arrays
  timedata.shift();
  if (timedata.length > 0) {
    console.log('Got timedata');
    //prints out the high level alert datetimes
    //for (var i= 0; i < highlevels.length; i++){
    //   console.log(highlevels[i][0]); 
    // }
  } else {
   // if fail return a old date to test.if after
   timedata = [[waybackdate, waybackdate, 0]];
  }
  return timedata;
}

function convertTimeDiff(atime) {
  //takes difference in minutes and converts using moment.js 
  //to more commonly used time references. 
  //outputs object {timevalue, timetype} where
  //timevalue = converted time to nearest tenth, and
  //timetype = type of time, e.g. minute, hour...year, etc
  difftime = moment.duration(atime,'minutes');
 	var years = difftime.years().toFixed(1);
  var days = difftime.days().toFixed(1);
	var months = difftime.months().toFixed(1);
  var weeks = difftime.weeks().toFixed(1);
  var hours = difftime.hours().toFixed(1);
	var minutes = Math.floor(difftime.minutes());
	//minutes equivatents: year:525600; month:43800, week:10080 day:1440, hour:60
  //use bit of common date leveling here.
  //use months to three years > 1576800 minutes
  //use days to 90, the shift to months > 129600 min
  //use hours to 36 hours > 2160 use days
  //use minutes up to 3 hours > 180 min use hours.; 
	if (atime >= 1576800.0) {
  	return  {timevalue:years, timetype:'years'};
  } else if (atime >= 129600.0) {
  	return {timevalue:months, timetype:'months'};
  } else if (atime >= 2160.0) {
  	return {timevalue:days, timetype:'days'};
  } else if (atime >= 180.0) {
  	return {timevalue:hours, timetype:'hours'};
  } else {
  	return {timevalue:minutes, timetype:'minutes'};
  }
}

app.handle ('last_alert', async conv =>{
  let spokenText1;
  let highdate;
  let conv_param;
  //try and use users input trigger word in reply
  conv_param = conv.intent.params.alert.original;
  console.log('in last_alert, attempting to go to sheet');
  var timedata = await getSheetData();
 //get last record
      //to console to see if worked
  if (moment(timedata[0][0]).isAfter(waybackdate)) {
   	let record;
  	let column = 0;
    const arrayColumn = (arr, n) => arr.map(x => x[n]);
    var highlevels = (arrayColumn(timedata, column)); 
  	//for (var i= 0; i < highlevels.length; i++){
    // console.log(timedata[i][0] + " " + timedata[i][1] + " " + timedata[i][2]); 
  	//}
  	// get the rows
  	record = timedata[highlevels.length-1];
  	console.log('last record is: ' + record[0]);
    var now = moment(new Date()); //todays date 
   	dtx = now.diff(record[0], "minutes");
    dt = convertTimeDiff(dtx);
    //console.log(dt.timevalue + " " + dt.timetype);
  	highdate = getSpokenDate(record[0]);
  	spokenText1 = "nothing yet";
  	if (record[1] != "") {
    	lowdate = getSpokenDate(record[1]);
    	// TO USE ${} template literals, use the backtic character (`) not single quotes.
     	spokenText1 =`The last high level ${conv_param} was ${highdate}, ${dt.timevalue} ${dt.timetype} ago, and ended ${lowdate} after ${record[2]} minutes.`;
  	} else {
      spokenText1 = `The last high level ${conv_param} was ${highdate}, ${dt.timevalue} ${dt.timetype} ago. No end date reported.`;
  	}
  	console.log(spokenText1); 
    //if conversation then output. if not output to card.
    conv.add(spokenText1);
    conv.add(new Card({
       title: 'Sump Pump Data',
       text: `Last alert: started: ${record[0]}; ended: ${record[1]}, ${record[2]} minutes later.`,
       })
    ); 
  } else { 
    conv.add('Something went wrong. No data was returned'); 
  } 
 }
);

app.handle ('range_alerts', async conv => {
  let spokenText1;
  //try and use users input trigger word in reply
  let rng_number;
  //let rng_all;
  let alert_param = conv.intent.params.alerts.original;
  
  //rng_all = conv.intent.params.all.original;
  //conv.add(`Range number is ${rng_number}`);
  //let session_num = conv.session.params.number.original;
  //conv.add(`Range number is ${session_number}`);
  console.log('in more_alert, attempting to go to sheet');
  var timedata = await getSheetData();
 //get last record
      //to console to see if worked
  if (moment(timedata[0][0]).isAfter(waybackdate)) {
  	let column = 0;
    //var highlevels = timedata.map(function(value,index) { return value[0]; });
   // console.log(col0);
    const arrayColumn = (arr, n) => arr.map(x => x[n]);
    var highlevels = arrayColumn(timedata, column);
    var now = moment(new Date()); //todays date 
    var dt;
    let dtx;
    if ('number' in conv.intent.params) {
      rng_number = conv.intent.params.number.original;
      //check and fix if number alerts exceeds number in sheet
      if (rng_number > highlevels.length) {
        rng_number = highlevels.length;
        conv.add(`There are only ${highlevels.length} alerts to read.`);
      }
    }
    if ('all' in conv.intent.params) {
      rng_number = highlevels.length;
    }
    //limit output to last 5 alerts
    if (rng_number > 5) {
      rng_number = 5;
      conv.add(`There are a total of ${highlevels.length} alerts, restricting to last five.`);
    }
    //console.log(dt.timevalue + " " + dt.timetype);
    //note use of 2 spaces before /n to get new line.
    let spokenText1 = `The last ${rng_number} high level ${alert_param} were, ` + "  \n";
    let subText = "";
    //conv.add(`Range number is ${rng_number}`);
    let startlevel = highlevels.length-rng_number;
    let highdate;
    
    if (rng_number > 1) {
      for (var i = startlevel; i<highlevels.length; i++) {
        //dtx is a moment object; not value a this point.
       	dtx = now.diff(highlevels[i] , 'minutes');
        dt = convertTimeDiff(dtx);
        highdate = getSpokenDate(highlevels[i]);
        subText = subText +  ` ${highdate}, ${dt.timevalue}, ${dt.timetype} ago.` + "  \n"; 
        }      
      spokenText1 = spokenText1 + subText;
    } else {
      spokenText1 = 'No data is available. Did you forget to give a range?';
    }
    conv.add(spokenText1);
    conv.add(new Card({
       title: 'Sump Pump Data',
       text: spokenText1,
       })
    );
    } else { 
      conv.add('Something went wrong. No data was returned'); 
    } 
  }
);

app.handle ('avg_alerts', async conv => {
  let spokenText1;
  let rng_number;
  var timedata = await getSheetData();
  //get last record
  //to console to see if worked
  if (moment(timedata[0][0]).isAfter(waybackdate)) {
   	let column = 0;
   	const arrayColumn = (arr, n) => arr.map(x => x[n]);
    var highlevels = arrayColumn(timedata, column);
     column = 2; //col C sheet data.
    var fixtimes = (arrayColumn(timedata, column));
   //console.log(`fixtime 0 is ${fixtimes[0]}`);
    //if ('average' in conv.intent.params) {
   //   let alert_param = conv.intent.params.average.original; 
   // }
    //Try to get range to average; default to all if fail. 
    if ('number' in conv.intent.params) {
      rng_number = conv.intent.params.number.resolved;
    } else {
     if ('all' in conv.intent.params) {
       rng_number = highlevels.length;
     } else {
       conv.add('Failed to find range to average. Using all data.');
       rng_number = highlevels.length;
       huh = 1;
     }
    }
    if ('number' in conv.intent.params) {
      rng_number = conv.intent.params.number.resolved;
      //check and fix if number alerts exceeds number in sheet
      if (rng_number > highlevels.length) {
        rng_number = highlevels.length;
      }
    }
    if ('all' in conv.intent.params) {
      rng_number = highlevels.length;
    }
    let rng_max = highlevels.length;
    //if can't get rng from user default to all values, but need -1
    if (rng_number < 2) {
      rng_number = highlevels.length-1;
    }
    //get averages fix times 
    let avgfix = 0;
    let teststuff = "";
    let startrow = highlevels.length - rng_number;
    //console.log(`rng_num is ${rng_num}; highlevels length is ${highlevels.length}`);
     for (var i = startrow; i < rng_max; i++) { 
      avgfix = avgfix + fixtimes[i]*1.0;
      teststuff = teststuff + ", " + avgfix;
    } 
    avgfix = avgfix/rng_number;
    //conv.add(teststuff);
    let dt = 0;
    let dtx;
    let h1;
    let h2;
    startrow = startrow;
    for (i= startrow-1; i < rng_number-1; i++) {
      h1 = moment(highlevels[i], 'MM/DD/YYYY HH:mm');
      h2 = moment(highlevels[i+1], 'MM/DD/YYYY HH:mm');
      dtx =(h2.diff(h1, "minutes")); //value returned as integer
      dt = dt + dtx; //all as minutes
    }
    //conv.add(`total is ${dt}`);
    var dtavg = convertTimeDiff(dt);
    var avghighlevel = (dtavg.timevalue)*1.0/rng_number; 
    var dtype = dtavg.timetype;
    
    spokenText1 = "nothing yet";
    	// TO USE ${} template leterals, use the backtic character (`) not single quotes.
    spokenText1 =`Average time between high level alerts is ${avghighlevel} ${dtype}, and average time to return to low level is ${avgfix} minutes.`;
  	
  	console.log(spokenText1); 
    conv.add(spokenText1);
    conv.add(new Card({
       title: 'Sump Pump Average Data',
       text: spokenText1,
       })
    ); 
  } else { 
    conv.add('No data is available. Did you forget to give a range?'); 
  }  
}
);

exports.ActionsOnGoogleFulfillment = functions.https.onRequest(app);           
