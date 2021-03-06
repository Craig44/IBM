#' @title plot_numeric_layer
#'
#' @description
#' A plotting function to generate a snapshot of heatmaps that can be used in an animation, to show how the 
#' the layer changes over time
#'
#' @author Craig Marsh
#' @param model <ibm_output> object that are generated from one of the extract() functions.
#' @param report_label <string>
#' @param directory <string> A directory to save the files to, default is getwd();
#' @return a png filed in the directory, with biomass binned up so that is binned up by 10 percentiles
#' and the legend is the midpoint of these bins.
#' @export 

plot_numeric_layer = function(model, report_label, directory = "", file_name = "", Title = "", breaks = NULL, width = NA, height = NA) {
  ## check report label exists
  if (!report_label %in% names(model))
    stop(Paste("In model the report label '", report_label, "' could not be found. The report labels available are ", paste(names(model),collapse = ", ")))
  ## get the report out
  report = get(report_label, model)
  
  if (any(names(report) %in% "type")) {
    if (report$type != "numeric_layer") {
      stop(Paste("The report label ", report_label, " in model is not a 'numeric_layer' plz Check you have specified the correct report_label."))     
    }
  } else {
    stop("multi iteration report found can't deal with this type")
    muliple_iterations_in_a_report = TRUE;
    N_runs = length(report);
    if (report$'1'$type != "numeric_layer") {
      stop(Paste("The report label ", report_label, " in model is not a 'numeric_layer' plz Check you have specified the correct report_label."))     
    }
  }
  if (directory == "")
    directory = getwd();
  
  ## go through data and find, min, max and some other data characteristics that might help with aesthetics
  vec_data = vector()
  for (i in 1:length(report)) {
    if( names(report)[i] != "type")
      vec_data = c(vec_data,as.numeric(report[[i]]$values))
  }
  if (is.null(breaks)) 
    breaks = c(min(vec_data) - 1, quantile(vec_data, c(0.1,0.2,0.3,0.4,0.5,0.6,0.7,0.8,0.9)), max(vec_data) + 1)
  
  dup_ndx = which(duplicated(breaks))
  ## because ggplot needs 10 breaks I am going to do a bit of a hack
  first_break = 0;
  for(i in 1:length(dup_ndx)) {
    if (i == 1)
      first_break = breaks[dup_ndx[i]]
    first_break = first_break + 0.1;
    breaks[dup_ndx[i]] = first_break
  }
  breaks = unique(breaks)

  cols = colorRampPalette(brewer.pal(9,"YlOrRd"))(length(breaks) - 1)
  legend_lab = vector();
  for (i in 1:(length(breaks) - 1)) {
    legend_lab[i] = round(breaks[i] + ((breaks[i + 1] - breaks[i])/2))
  }  
  if (file_name == "")
    file_name = report_label
  #dir.create("Figures")
  setwd(directory)
  for (i in 1:length(report)) {
    if( names(report)[i] != "type") {
      this_data = melt(report[[i]]$values)
      brks <- cut(this_data$value,breaks)
      brks = factor(brks, levels=levels(brks))
      this_data$brks <- brks
      P = ggplot(this_data, aes(y = rev(Var1), x = Var2,fill=brks))  + geom_tile() +
        #redrawing tiles to remove cross lines from legend
        geom_tile(colour="white",size=0.25, show.legend =FALSE) +
        ggtitle(paste0(report[[i]]$year, " " , Title))+
        labs(x="",y="")+
        scale_y_discrete(expand=c(0,0)) +
        scale_x_discrete(expand=c(0,0)) + 
        scale_fill_manual("Key", values=cols,na.value="grey90",labels = round(breaks), guide = guide_legend(reverse=T),drop = FALSE)
      ggsave(P, filename = paste0(report[[i]]$year,"_", file_name,".png"), width = width, height = height)
    }
  }
  return (NULL);
}
  