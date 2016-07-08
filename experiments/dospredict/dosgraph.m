%columns in the script are #patterns, dospackets fraction, threshold, #packets, #loss, #unfinished sweeps
x=csvread('dosmeasure3.txt');
d = unique(x(:,1));
d2 = unique(x(:,2));
d2(d2==1)=[];
couldrun = x(:,4)>0;
noloss = x(:,5)<10e4;
nonotfinish = x(:,6)<2;
clear m; for i = 1:length(d), for j = 1:length(d2), di = d(i); d2i = d2(j); m(i,j,:)=[di,d2i,min(x(and(and(x(:,1)==di,x(:,2)==d2i),and(and(couldrun, noloss),nonotfinish)),3))]; end; end;

budget =  [31772834447 23302599043 16409706978.5 10280450287]/20; % todrain, for different % of dos traffic
sweep = 100;
nomatch = [376 570 942 1884];% for different # patterns
match = [607 906 1446 2456]; %from run
flowlen = 10;
matchperflow = 8;
f2 = 1.5;
clear t;
for i = 1:length(d), 
    for j = 1:length(d2), 
        di = d(i); 
        d2i = d2(j);
        rate = 10^10/((64*d2i+1500 *(1-d2i)+20)*8);
        % solve for t: b == match(i) *(d2i*rate/t + (1 - d2i)*rate/flowlen)
        % + 1.5 *matchperflow*sweep*(1 - d2i)*rate/flowlen
        %solve for t: b == match(i) *(d2i*rate/t + (1 - d2i)*rate/flowlen) + 1.5*n*sweep*(1 - d2i)*rate/flowlen
        %t = (d2i * flowlen* rate* (match(i) + 1.5 *matchperflow* sweep))/(budget * flowlen - rate * match(i)* (1-d2i) - 1.5*(1-d2i)*rate*matchperflow* sweep);
        t =  d2i *flowlen *match(i) *rate/(budget(j) *flowlen - rate *((1-d2i) *match(i) + 1.5*(1-d2i) * matchperflow * sweep));
        %pktnum = rate * 20;
        %flownum =  d2i*pktnum/20 + (1-d2i)*pktnum/10;
        threshold(i,j,:) = [di, d2i, ceil(t)];
    end
end

marks={'-vb','-^g','-sr','-ok'};
marks2={'--vb','--^g','--sr','--ok'};
figure; hold all; 
for i2=1:size(m,2), 
    i=size(m,2)-i2+1;
    plot(d, m(:,i,3)*64/1000,marks{i},'LineWidth',2); 
end
for i2=1:size(m,2), 
    i=size(m,2)-i2+1;
    plot(d, threshold(:,i,3)*64/1000, marks2{i},'LineWidth',2); 
end
% mean(mean(m(:,:,3)- threshold(:,:,3))*64/1000)
% for i=1:size(m,2)
%     mean(m(:,i,3)- threshold(:,i,3))*64/1000
% end

set(gca,'xscale','log')
set(gca,'XTick', [8 16 32 64])
xlim([7,68]);
xlabel('# patterns');
ylabel('Threshold (KB)');
set(findobj(gcf, 'type','axes'),'fontsize',14)
set(findall(gcf,'type','text'),'fontSize',14')
set(gcf,'OuterPosition',[500,500,375,360])
l=columnlegend(2,[{'75'};{'50'};{'25'};{'75 p'};{'50 p'};{'25 p'}],'Location','NorthWest', 'fontsize',12);
